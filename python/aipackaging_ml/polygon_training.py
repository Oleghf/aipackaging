"""Воспроизводимое обучение полигональной политики через BC и PPO."""

from __future__ import annotations

import math
import os
import random
import time
import hashlib
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np
import torch

from . import _aipackaging_solver as _native
from .datasets.serialization import canonical_json, sha256_file, write_canonical_json
from .polygon_contracts import load_polygon_training_config
from .polygon_model import HierarchicalPolygonPolicyV1
from .polygon_policy import (
    PolygonPolicyRunner,
    encode_polygon_observation,
    evaluate_polygon_components,
    select_polygon_pair,
    select_polygon_position,
)
from .polygon_training_cache import CachedPolygonExpertStep, load_or_build_polygon_observation_cache
from .polygon_training_data import PolygonExpertEpisode, load_polygon_baseline_solutions, load_polygon_expert_episodes
from .rl import compute_gae
from .rollout import MultiprocessRolloutPool
from .training import configure_determinism


@dataclass(frozen=True)
class PolygonPpoTransition:
    """Хранит компактное состояние и условное наблюдение выбранной пары."""

    fixed: Mapping[str, Any]
    dynamic: Mapping[str, Any]
    placement: Mapping[str, Any]
    instance: int
    rotation: int
    position: int
    old_log_probability: float
    old_value: float
    reward: float
    next_value: float
    terminated: bool
    trace_end: bool = False


@dataclass(frozen=True)
class PolygonTrainingBudget:
    """Учитывает единый временной бюджет во всех продолжениях обучения."""

    limit_seconds: float
    elapsed_before_seconds: float
    invocation_started: float

    @property
    def deadline(self) -> float:
        """Возвращает момент исчерпания оставшейся части общего бюджета."""

        return self.invocation_started + max(0.0, self.limit_seconds - self.elapsed_before_seconds)

    def elapsed_seconds(self) -> float:
        """Возвращает накопленное время, ограниченное полным бюджетом."""

        current = self.elapsed_before_seconds + max(0.0, time.monotonic() - self.invocation_started)
        return min(self.limit_seconds, current)

    def checkpoint_state(self, values: Mapping[str, Any]) -> dict[str, Any]:
        """Добавляет накопленное время к состоянию контрольной точки."""

        result = dict(values)
        result["elapsedTrainingSeconds"] = self.elapsed_seconds()
        return result


def _resume_elapsed_seconds(payload: Mapping[str, Any], limit_seconds: float) -> float:
    """Проверяет накопленное время перед продолжением контрольной точки."""

    training_state = payload.get("trainingState")
    if not isinstance(training_state, Mapping) or "elapsedTrainingSeconds" not in training_state:
        raise ValueError(
            "контрольная точка не содержит накопленное время; её можно оценить или завершить, но нельзя продолжить"
        )
    elapsed = training_state["elapsedTrainingSeconds"]
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not math.isfinite(float(elapsed)):
        raise ValueError("контрольная точка содержит некорректное накопленное время")
    elapsed_value = float(elapsed)
    if elapsed_value < 0 or elapsed_value > limit_seconds:
        raise ValueError("накопленное время контрольной точки выходит за общий бюджет")
    if elapsed_value >= limit_seconds:
        raise ValueError("общий временной бюджет обучения уже исчерпан")
    return elapsed_value


def _budget_state(budget: PolygonTrainingBudget | None, values: Mapping[str, Any]) -> dict[str, Any]:
    """Возвращает состояние с накопленным временем, если бюджет предоставлен."""

    return budget.checkpoint_state(values) if budget is not None else dict(values)


def _checkpoint_payload(model: HierarchicalPolygonPolicyV1, optimizer: torch.optim.Optimizer,
                        scheduler: torch.optim.lr_scheduler.LRScheduler, *, stage: str, step: int,
                        config: Mapping[str, Any], training_state: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Собирает полное состояние обучения для точного продолжения."""

    payload: dict[str, Any] = {
        "format": "aipackaging.polygon_training_checkpoint", "version": 1, "stage": stage, "step": step,
        "config": dict(config), "modelState": model.state_dict(), "optimizerState": optimizer.state_dict(),
        "schedulerState": scheduler.state_dict(), "pythonRandomState": random.getstate(),
        "numpyRandomState": np.random.get_state(), "torchRandomState": torch.get_rng_state(),
        "trainingState": dict(training_state or {}),
    }
    if torch.cuda.is_available():
        payload["cudaRandomState"] = torch.cuda.get_rng_state_all()
    return payload


def save_polygon_checkpoint(path: str | Path, model: HierarchicalPolygonPolicyV1, optimizer: torch.optim.Optimizer,
                            scheduler: torch.optim.lr_scheduler.LRScheduler, *, stage: str, step: int,
                            config: Mapping[str, Any], training_state: Mapping[str, Any] | None = None) -> str:
    """Атомарно сохраняет контрольную точку и возвращает её SHA-256."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    torch.save(_checkpoint_payload(model, optimizer, scheduler, stage=stage, step=step, config=config,
                                   training_state=training_state), temporary)
    temporary.replace(destination)
    digest = sha256_file(destination)
    digest_path = destination.with_suffix(destination.suffix + ".sha256")
    digest_temporary = digest_path.with_suffix(digest_path.suffix + ".tmp")
    digest_temporary.write_text(digest + "\n", encoding="ascii", newline="\n")
    digest_temporary.replace(digest_path)
    return digest


def load_polygon_checkpoint(path: str | Path, model: HierarchicalPolygonPolicyV1, device: torch.device, *,
                            optimizer: torch.optim.Optimizer | None = None,
                            scheduler: torch.optim.lr_scheduler.LRScheduler | None = None,
                            restore_rng: bool = False, expected_sha256: str | None = None) -> dict[str, Any]:
    """Проверяет хеш и загружает доверенную локальную контрольную точку."""

    source = Path(path)
    if expected_sha256 is None:
        digest_path = source.with_suffix(source.suffix + ".sha256")
        if not digest_path.is_file():
            raise ValueError("рядом с контрольной точкой отсутствует файл SHA-256")
        expected_sha256 = digest_path.read_text(encoding="ascii").strip()
    if len(expected_sha256) != 64 or any(character not in "0123456789abcdef" for character in expected_sha256.lower()):
        raise ValueError("некорректная запись SHA-256 контрольной точки")
    if sha256_file(source) != expected_sha256.lower():
        raise ValueError("контрольная сумма контрольной точки не совпадает")
    payload = torch.load(source, map_location=device, weights_only=False)
    if payload.get("format") != "aipackaging.polygon_training_checkpoint" or payload.get("version") != 1:
        raise ValueError("неподдерживаемая контрольная точка полигонального обучения")
    required = {
        "format", "version", "stage", "step", "config", "modelState", "optimizerState", "schedulerState",
        "pythonRandomState", "numpyRandomState", "torchRandomState", "trainingState",
    }
    actual_fields = set(payload)
    if actual_fields != required and actual_fields != required | {"cudaRandomState"}:
        raise ValueError("контрольная точка содержит неизвестные или пропущенные поля")
    if payload["stage"] not in {"bc", "ppo"} or not isinstance(payload["step"], int) or payload["step"] < 0:
        raise ValueError("контрольная точка содержит некорректный этап обучения")
    model.load_state_dict(payload["modelState"])
    if optimizer is not None:
        optimizer.load_state_dict(payload["optimizerState"])
    if scheduler is not None:
        scheduler.load_state_dict(payload["schedulerState"])
    if restore_rng:
        random.setstate(payload["pythonRandomState"])
        np.random.set_state(payload["numpyRandomState"])
        # `map_location` переносит все тензоры контрольной точки на устройство
        # модели, но основной генератор PyTorch принимает состояние только с CPU.
        torch.set_rng_state(payload["torchRandomState"].cpu())
        if torch.cuda.is_available() and "cudaRandomState" in payload:
            torch.cuda.set_rng_state_all([state.cpu() for state in payload["cudaRandomState"]])
    return payload


def _validation_nll(model: HierarchicalPolygonPolicyV1, samples: Sequence[CachedPolygonExpertStep],
                    device: torch.device, deadline: float | None) -> float | None:
    """Вычисляет среднюю отрицательную логарифмическую вероятность эксперта."""

    model.eval()
    total = 0.0
    count = 0
    with torch.no_grad():
        for sample in samples:
            if deadline is not None and time.monotonic() >= deadline:
                return None
            total -= float(evaluate_polygon_components(
                model, sample.fixed, sample.dynamic, sample.placement,
                sample.instance, sample.rotation, sample.position, device,
            ).log_probability)
            count += 1
    return total / max(count, 1)


def train_polygon_bc(model: HierarchicalPolygonPolicyV1, train_samples: Sequence[CachedPolygonExpertStep],
                     validation_samples: Sequence[CachedPolygonExpertStep], config: Mapping[str, Any],
                     run_dir: Path, device: torch.device, *, smoke: bool, deadline: float,
                     resume: str | Path | None = None,
                     budget: PolygonTrainingBudget | None = None) -> tuple[Path, list[dict[str, Any]], str]:
    """Обучает три головы по экспертным действиям и критик по оставшейся отдаче."""

    settings = config["behavioralCloning"]
    optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"], weight_decay=settings["weightDecay"])
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    epochs = 1 if smoke else settings["maxEpochs"]
    accumulation = settings["gradientAccumulation"]
    best_path = run_dir / "polygon-bc-best.pt"
    resume_path = run_dir / "polygon-resume.pt"
    history: list[dict[str, Any]] = []
    best_nll = math.inf
    stale = 0
    start_epoch = 0
    if resume is not None:
        payload = load_polygon_checkpoint(resume, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
        if payload["stage"] != "bc":
            raise ValueError("для продолжения BC требуется контрольная точка этапа BC")
        start_epoch = int(payload["step"])
        best_nll = float(payload.get("trainingState", {}).get("bestNll", math.inf))
        stale = int(payload.get("trainingState", {}).get("stale", 0))
    status = "complete"
    for epoch in range(start_epoch, epochs):
        if time.monotonic() >= deadline:
            status = "budget_exhausted"
            break
        model.train()
        optimizer.zero_grad(set_to_none=True)
        samples = 0
        total_loss = 0.0
        order = list(range(len(train_samples)))
        random.Random(config["seed"] + epoch).shuffle(order)
        exhausted = False
        for sample_index in order:
            if time.monotonic() >= deadline:
                exhausted = True
                break
            sample = train_samples[sample_index]
            decision = evaluate_polygon_components(
                model, sample.fixed, sample.dynamic, sample.placement,
                sample.instance, sample.rotation, sample.position, device,
            )
            target = torch.tensor(sample.value_target, dtype=torch.float32, device=device)
            loss = -decision.log_probability + 0.5 * torch.square(decision.value - target)
            (loss / accumulation).backward()
            total_loss += float(loss.detach())
            samples += 1
            if samples % accumulation == 0:
                torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                optimizer.step()
                optimizer.zero_grad(set_to_none=True)
        if samples % accumulation:
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
        if exhausted:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch, config=config,
                                    training_state=_budget_state(budget, {"bestNll": best_nll, "stale": stale}))
            status = "budget_exhausted"
            break
        scheduler.step()
        nll = _validation_nll(model, validation_samples, device, deadline)
        if nll is None:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config,
                                    training_state=_budget_state(budget, {"bestNll": best_nll, "stale": stale}))
            status = "budget_exhausted"
            break
        history.append({"stage": "bc", "epoch": epoch + 1, "samples": samples,
                        "trainingLoss": total_loss / max(samples, 1), "validationNll": nll})
        if nll < best_nll:
            best_nll = nll
            stale = 0
            save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config,
                                    training_state=_budget_state(budget, {"bestNll": best_nll, "stale": stale}))
        else:
            stale += 1
            if stale >= settings["earlyStoppingPatience"]:
                break
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config,
                                training_state=_budget_state(budget, {"bestNll": best_nll, "stale": stale}))
    if not best_path.exists():
        save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="bc", step=len(history), config=config,
                                training_state=_budget_state(budget, {"bestNll": best_nll, "stale": stale}))
    load_polygon_checkpoint(best_path, model, device)
    return best_path, history, status


def _collect_transitions(model: HierarchicalPolygonPolicyV1, episodes: Sequence[PolygonExpertEpisode], count: int,
                         device: torch.device, seed: int, workers: int, deadline: float) -> list[PolygonPpoTransition]:
    """Собирает переходы, выполняя геометрию в независимых рабочих процессах."""

    model.eval()
    lanes = min(workers, count)
    order = list(range(len(episodes)))
    random.Random(seed).shuffle(order)
    cursor = 0
    generators: list[torch.Generator] = []

    def next_task() -> tuple[Mapping[str, Any], int]:
        """Возвращает следующую задачу и производное начальное значение канала."""

        nonlocal cursor
        episode = episodes[order[cursor % len(order)]]
        episode_seed = seed + cursor + 1
        cursor += 1
        return episode.problem, episode_seed

    initial_tasks = [next_task() for _ in range(lanes)]
    for _, episode_seed in initial_tasks:
        generator = torch.Generator(device=device)
        generator.manual_seed(episode_seed)
        generators.append(generator)
    lane_results: list[list[PolygonPpoTransition]] = [[] for _ in range(lanes)]
    collected = 0
    with MultiprocessRolloutPool(lanes, environment_kind="polygon") as pool:
        observations = pool.reset(initial_tasks)
        fixed_values = [item[0] for item in observations]
        dynamic_values = [item[1] for item in observations]
        with torch.no_grad():
            while collected < count and time.monotonic() < deadline:
                # CUDA выбирает пары в главном процессе, после чего рабочие процессы
                # параллельно строят дорогие динамические каталоги размещений.
                active_lanes = range(min(lanes, count - collected))
                pairs = {
                    lane: select_polygon_pair(model, fixed_values[lane], dynamic_values[lane], device, generators[lane])
                    for lane in active_lanes
                }
                placements = pool.polygon_placements_lanes({
                    lane: (pair.instance_position, pair.rotation_slot) for lane, pair in pairs.items()
                })
                decisions = {
                    lane: select_polygon_position(model, pairs[lane], placements[lane], device, generators[lane])
                    for lane in active_lanes
                }
                actions = {lane: placements[lane]["actions"][decisions[lane].position_index] for lane in active_lanes}
                step_results = pool.polygon_step_actions_lanes(actions)
                reset_tasks: dict[int, tuple[Mapping[str, Any], int]] = {}
                for lane, (_, result) in step_results.items():
                    next_dynamic, reward, terminated, _, _ = result
                    next_value = 0.0 if terminated else float(
                        encode_polygon_observation(model, fixed_values[lane], next_dynamic, device).value
                    )
                    decision = decisions[lane]
                    lane_results[lane].append(PolygonPpoTransition(
                        fixed_values[lane], dynamic_values[lane], placements[lane],
                        decision.instance_position, decision.rotation_slot, decision.position_index,
                        float(decision.log_probability), float(decision.value), float(reward), next_value, terminated,
                    ))
                    collected += 1
                    if terminated:
                        reset_tasks[lane] = next_task()
                    else:
                        dynamic_values[lane] = next_dynamic
                if reset_tasks:
                    reset_values = pool.reset_lanes(reset_tasks)
                    for lane, (fixed, dynamic, _) in reset_values.items():
                        fixed_values[lane] = fixed
                        dynamic_values[lane] = dynamic
                        generator = torch.Generator(device=device)
                        generator.manual_seed(reset_tasks[lane][1])
                        generators[lane] = generator
    for lane in lane_results:
        if lane:
            lane[-1] = replace(lane[-1], trace_end=True)
    return [item for lane in lane_results for item in lane]


def _advantages(transitions: Sequence[PolygonPpoTransition], gamma: float, gae_lambda: float) -> tuple[np.ndarray, np.ndarray]:
    """Преобразует полигональные переходы к общему вычислению GAE."""

    proxies = [type("Transition", (), {"reward": item.reward, "next_value": item.next_value,
                                       "old_value": item.old_value, "terminated": item.terminated,
                                       "trace_end": item.trace_end})() for item in transitions]
    return compute_gae(proxies, gamma, gae_lambda)


def _validation_score(model: HierarchicalPolygonPolicyV1, episodes: Sequence[PolygonExpertEpisode],
                      baselines: Mapping[str, Mapping[str, Mapping[str, Any]]], device: torch.device,
                      seed: int, deadline: float) -> tuple[tuple[float, ...], dict[str, Any]] | None:
    """Оценивает полноту и победы жадной политики над замороженным случайным алгоритмом."""

    runner = PolygonPolicyRunner(model, model_id="validation", model_sha256="0" * 64, device=device)
    solved = wins = 0
    used_lengths: list[int] = []
    for episode in episodes:
        if time.monotonic() >= deadline:
            return None
        solution = runner.solve(episode.problem, mode="greedy", seed=seed)
        solved += solution["status"] == "solved"
        if solution["status"] == "solved":
            used_lengths.append(solution["objective"]["usedLengthMicrometers"])
        reference = baselines[episode.problem["problemId"]]["random-left-bottom"]
        wins += bool(_native.is_better_polygon_solution(canonical_json(solution), canonical_json(reference)))
    mean_used = sum(used_lengths) / max(len(used_lengths), 1)
    return (float(solved), float(wins), -mean_used), {"solved": solved, "tasks": len(episodes), "winsVsRandom": wins,
                                                       "meanUsedLengthMicrometers": mean_used}


def train_polygon_ppo(model: HierarchicalPolygonPolicyV1, train_episodes: Sequence[PolygonExpertEpisode],
                      validation_episodes: Sequence[PolygonExpertEpisode], validation_baselines: Mapping[str, Any],
                      config: Mapping[str, Any], run_dir: Path, device: torch.device, *, smoke: bool,
                      deadline: float, resume: str | Path | None = None,
                      budget: PolygonTrainingBudget | None = None) -> tuple[Path, list[dict[str, Any]], str]:
    """Дообучает BC-модель методом PPO и сохраняет лучший проверочный результат."""

    settings = config["ppo"]
    optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"])
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    updates = 1 if smoke else settings["updates"]
    count = min(8, settings["transitionsPerUpdate"]) if smoke else settings["transitionsPerUpdate"]
    epochs = 1 if smoke else settings["epochsPerUpdate"]
    best_path = run_dir / "polygon-ppo-best.pt"
    resume_path = run_dir / "polygon-resume.pt"
    start = 0
    if resume is not None:
        payload = load_polygon_checkpoint(resume, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
        if payload["stage"] != "ppo":
            raise ValueError("для продолжения PPO требуется контрольная точка этапа PPO")
        start = int(payload["step"])
    stored_score = payload.get("trainingState", {}).get("bestScore") if resume is not None else None
    best_score: tuple[float, ...] | None = tuple(stored_score) if stored_score is not None else None
    history: list[dict[str, Any]] = []
    status = "complete"
    interval = 1 if smoke else max(1, updates // 10)
    for update in range(start, updates):
        if time.monotonic() >= deadline:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_budget_state(
                                        budget, {"bestScore": list(best_score) if best_score is not None else None}
                                    ))
            status = "budget_exhausted"; break
        transitions = _collect_transitions(model, train_episodes, count, device, config["seed"] + update,
                                           settings["workers"], deadline)
        if len(transitions) != count:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_budget_state(
                                        budget, {"bestScore": list(best_score) if best_score is not None else None}
                                    ))
            status = "budget_exhausted"; break
        # Файл содержит только согласованное состояние до текущего обновления.
        # При истечении времени частично применённые градиенты будут отброшены.
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                training_state=_budget_state(
                                    budget, {"bestScore": list(best_score) if best_score is not None else None}
                                ))
        if time.monotonic() >= deadline:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_budget_state(
                                        budget, {"bestScore": list(best_score) if best_score is not None else None}
                                    ))
            status = "budget_exhausted"; break
        advantages, returns = _advantages(transitions, settings["gamma"], settings["gaeLambda"])
        advantages = (advantages - advantages.mean()) / max(float(advantages.std()), 1e-8)
        entropy = settings["entropyStart"] + (settings["entropyEnd"] - settings["entropyStart"]) * update / max(updates - 1, 1)
        model.train(); total_loss = 0.0
        exhausted = False
        for epoch in range(epochs):
            indices = list(range(len(transitions)))
            random.Random(config["seed"] + update * 1009 + epoch).shuffle(indices)
            optimizer.zero_grad(set_to_none=True)
            for local, index in enumerate(indices, 1):
                if time.monotonic() >= deadline:
                    exhausted = True
                    break
                item = transitions[index]
                decision = evaluate_polygon_components(model, item.fixed, item.dynamic, item.placement,
                                                       item.instance, item.rotation, item.position, device)
                if time.monotonic() >= deadline:
                    exhausted = True
                    break
                ratio = torch.exp(decision.log_probability - torch.tensor(item.old_log_probability, device=device))
                advantage = torch.tensor(float(advantages[index]), device=device)
                actor = -torch.minimum(ratio * advantage,
                                       torch.clamp(ratio, 1 - settings["clipRatio"], 1 + settings["clipRatio"]) * advantage)
                value = torch.square(decision.value - torch.tensor(float(returns[index]), device=device))
                loss = actor + settings["valueCoefficient"] * value - entropy * decision.entropy
                if time.monotonic() >= deadline:
                    exhausted = True
                    break
                (loss / 64).backward(); total_loss += float(loss.detach())
                if local % 64 == 0 or local == len(indices):
                    if time.monotonic() >= deadline:
                        exhausted = True
                        break
                    torch.nn.utils.clip_grad_norm_(model.parameters(), settings["maxGradientNorm"])
                    optimizer.step(); optimizer.zero_grad(set_to_none=True)
            if exhausted:
                break
        if exhausted or time.monotonic() >= deadline:
            load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_budget_state(
                                        budget, {"bestScore": list(best_score) if best_score is not None else None}
                                    ))
            status = "budget_exhausted"
            break
        scheduler.step()
        record: dict[str, Any] = {"stage": "ppo", "update": update + 1, "transitions": count,
                                  "loss": total_loss / max(count * epochs, 1), "entropyCoefficient": entropy}
        if (update + 1) % interval == 0 or update + 1 == updates:
            validation = _validation_score(model, validation_episodes, validation_baselines, device, config["seed"], deadline)
            if validation is None:
                load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
                save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                        training_state=_budget_state(
                                            budget, {"bestScore": list(best_score) if best_score is not None else None}
                                        ))
                status = "budget_exhausted"; break
            score, details = validation; record["validation"] = details
            if best_score is None or score > best_score:
                best_score = score
                save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="ppo", step=update + 1, config=config,
                                        training_state=_budget_state(budget, {"bestScore": list(best_score)}))
        history.append(record)
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update + 1, config=config,
                                training_state=_budget_state(
                                    budget, {"bestScore": list(best_score) if best_score is not None else None}
                                ))
    if not best_path.exists():
        save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="ppo", step=start + len(history), config=config,
                                training_state=_budget_state(
                                    budget, {"bestScore": list(best_score) if best_score is not None else None}
                                ))
    load_polygon_checkpoint(best_path, model, device)
    return best_path, history, status


def train_polygon_pipeline(config_path: str | Path, dataset_root: str | Path, run_dir: str | Path, *,
                           device_name: str = "cuda", smoke: bool = False,
                           resume: str | Path | None = None) -> dict[str, Any]:
    """Выполняет BC → PPO и записывает проверяемый манифест запуска."""

    config = load_polygon_training_config(config_path)
    device = torch.device(device_name)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("полигональное обучение требует доступного устройства CUDA")
    configure_determinism(config["seed"])
    invocation_started = time.monotonic()
    output = Path(run_dir); output.mkdir(parents=True, exist_ok=True)
    write_canonical_json(output / "training-config.json", config)
    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"]).to(device)
    resume_payload = load_polygon_checkpoint(resume, model, device) if resume is not None else None
    limit_seconds = float(config["ppo"]["maxWallTimeSeconds"])
    elapsed_before = _resume_elapsed_seconds(resume_payload, limit_seconds) if resume_payload is not None else 0.0
    budget = PolygonTrainingBudget(limit_seconds, elapsed_before, invocation_started)
    deadline = budget.deadline
    train = load_polygon_expert_episodes(dataset_root, "train", expected_manifest_sha256=config["datasetManifestSha256"])
    validation = load_polygon_expert_episodes(dataset_root, "validation", expected_manifest_sha256=config["datasetManifestSha256"])
    baselines = load_polygon_baseline_solutions(dataset_root, "validation", expected_manifest_sha256=config["datasetManifestSha256"])
    if smoke:
        train = train[:1]; validation = validation[:1]
        baselines = {validation[0].problem["problemId"]: baselines[validation[0].problem["problemId"]]}
    cache_identity = {
        "datasetManifestSha256": config["datasetManifestSha256"],
        "rewardVersion": config["rewardVersion"],
        "model": config["model"],
        "nativeVersion": _native.__version__,
        "nativeRevision": _native.__revision__,
        "nativeModuleSha256": sha256_file(Path(_native.__file__)),
        "smoke": smoke,
    }
    cache_fingerprint = hashlib.sha256(canonical_json(cache_identity).encode("utf-8")).hexdigest()
    cache_path = output / "polygon-observation-cache.zip"
    cached = load_or_build_polygon_observation_cache(
        cache_path, {"train": train, "validation": validation}, cache_fingerprint
    )
    bc_history: list[dict[str, Any]] = []
    ppo_resume = None
    if resume is None:
        bc_path, bc_history, bc_status = train_polygon_bc(model, cached["train"], cached["validation"], config, output, device,
                                                         smoke=smoke, deadline=deadline, budget=budget)
        if bc_status != "complete":
            ppo_path = bc_path; ppo_history = []; status = bc_status
        else:
            ppo_path, ppo_history, status = train_polygon_ppo(model, train, validation, baselines, config, output,
                                                             device, smoke=smoke, deadline=deadline, budget=budget)
    else:
        payload = resume_payload
        assert payload is not None
        bc_path = output / "polygon-bc-best.pt"
        if not bc_path.exists():
            bc_path = Path(resume)
        ppo_resume = resume if payload["stage"] == "ppo" else None
        if payload["stage"] == "bc":
            bc_path, bc_history, bc_status = train_polygon_bc(model, cached["train"], cached["validation"], config, output, device,
                                                             smoke=smoke, deadline=deadline, resume=resume, budget=budget)
        else:
            bc_status = "complete"
        if bc_status == "complete":
            ppo_path, ppo_history, status = train_polygon_ppo(model, train, validation, baselines, config, output,
                                                             device, smoke=smoke, deadline=deadline, resume=ppo_resume,
                                                             budget=budget)
        else:
            ppo_path = bc_path; ppo_history = []; status = bc_status
    metrics_path = output / "metrics.jsonl"
    metrics_path.write_text("".join(canonical_json(item) + "\n" for item in (*bc_history, *ppo_history)),
                            encoding="utf-8", newline="\n")
    config_hash = sha256_file(config_path)
    checkpoint_hash = sha256_file(ppo_path)
    policy_path = output / "polygon-policy.json"
    policy = {"format": "aipackaging.polygon_policy", "version": 1, "modelId": "hierarchical-polygon-policy-v1",
              "architecture": config["model"]["architecture"], "observationVersion": 2,
              "rewardVersion": config["rewardVersion"], "datasetManifestSha256": config["datasetManifestSha256"],
              "configSha256": config_hash, "checkpointSha256": checkpoint_hash, "limits": dict(config["model"]),
              "actionSelection": {"levels": ["instance", "rotation", "position"], "tieBreak": "lowest-current-catalog-index"}}
    write_canonical_json(policy_path, policy)
    manifest = {"format": "aipackaging.polygon_training_run", "version": 1, "status": status,
                "seed": config["seed"], "device": str(device), "datasetManifestSha256": config["datasetManifestSha256"],
                "configSha256": config_hash,
                "bcCheckpoint": {"path": bc_path.name, "sha256": sha256_file(bc_path)},
                "ppoCheckpoint": {"path": ppo_path.name, "sha256": checkpoint_hash},
                "policy": {"path": policy_path.name, "sha256": sha256_file(policy_path)},
                "metrics": {"path": metrics_path.name, "sha256": sha256_file(metrics_path)},
                "observationCache": {"path": cache_path.name, "sha256": sha256_file(cache_path)},
                "software": {"python": os.sys.version.split()[0], "torch": torch.__version__, "cuda": torch.version.cuda or "none"}}
    write_canonical_json(output / "run-manifest.json", manifest)
    return manifest
