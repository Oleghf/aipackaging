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
from .training_checkpoint import CheckpointContract, load_training_checkpoint, save_training_checkpoint
from .training_runtime import TrainingBudget, committed_state, resume_elapsed_seconds, restore_committed_state, write_metrics_history


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


PolygonTrainingBudget = TrainingBudget
_resume_elapsed_seconds = resume_elapsed_seconds

_POLYGON_CHECKPOINT = CheckpointContract(
    format="aipackaging.polygon_training_checkpoint",
    unsupported_message="неподдерживаемая контрольная точка полигонального обучения",
    strict_fields=True,
    write_sha256_sidecar=True,
    require_training_state=True,
)


def _bc_training_state(budget: PolygonTrainingBudget | None, epoch: int, best_nll: float, stale: int,
                       history: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    """Отмечает подтверждённую границу эпохи и сохраняет историю выбора модели."""

    return committed_state(
        "bc", epoch, history, budget=budget, values={"bestNll": best_nll, "stale": stale}
    )


def _ppo_training_state(
    budget: PolygonTrainingBudget | None,
    update: int,
    best_score: Sequence[float] | None,
    history: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
    """Отмечает подтверждённую границу обновления PPO и сохраняет полную историю запуска."""

    return committed_state(
        "ppo",
        update,
        history,
        budget=budget,
        values={"bestScore": list(best_score) if best_score is not None else None},
    )


def save_polygon_checkpoint(path: str | Path, model: HierarchicalPolygonPolicyV1, optimizer: torch.optim.Optimizer,
                            scheduler: torch.optim.lr_scheduler.LRScheduler, *, stage: str, step: int,
                            config: Mapping[str, Any], training_state: Mapping[str, Any] | None = None) -> str:
    """Атомарно сохраняет контрольную точку и возвращает её SHA-256."""

    return save_training_checkpoint(
        path,
        _POLYGON_CHECKPOINT,
        model,
        optimizer,
        scheduler,
        stage=stage,
        step=step,
        config=config,
        training_state=training_state or {},
    )


def load_polygon_checkpoint(path: str | Path, model: HierarchicalPolygonPolicyV1, device: torch.device, *,
                            optimizer: torch.optim.Optimizer | None = None,
                            scheduler: torch.optim.lr_scheduler.LRScheduler | None = None,
                            restore_rng: bool = False, expected_sha256: str | None = None,
                            expected_config: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Проверяет хеш и загружает доверенную локальную контрольную точку."""

    return load_training_checkpoint(
        path,
        _POLYGON_CHECKPOINT,
        model,
        device,
        optimizer=optimizer,
        scheduler=scheduler,
        restore_rng=restore_rng,
        expected_sha256=expected_sha256,
        expected_config=expected_config,
    )


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


def _train_polygon_bc_epoch(
    model: HierarchicalPolygonPolicyV1,
    samples: Sequence[CachedPolygonExpertStep],
    optimizer: torch.optim.Optimizer,
    *,
    accumulation: int,
    seed: int,
    epoch: int,
    device: torch.device,
    deadline: float,
) -> tuple[int, float, bool]:
    """Выполняет одну полигональную эпоху BC без публикации её результата."""

    model.train()
    optimizer.zero_grad(set_to_none=True)
    order = list(range(len(samples)))
    random.Random(seed + epoch).shuffle(order)
    count = 0
    total_loss = 0.0
    for sample_index in order:
        if time.monotonic() >= deadline:
            return count, total_loss, True
        sample = samples[sample_index]
        decision = evaluate_polygon_components(
            model,
            sample.fixed,
            sample.dynamic,
            sample.placement,
            sample.instance,
            sample.rotation,
            sample.position,
            device,
        )
        target = torch.tensor(sample.value_target, dtype=torch.float32, device=device)
        loss = -decision.log_probability + 0.5 * torch.square(decision.value - target)
        (loss / accumulation).backward()
        total_loss += float(loss.detach())
        count += 1
        if count % accumulation == 0:
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
    if time.monotonic() >= deadline:
        return count, total_loss, True
    if count % accumulation:
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optimizer.step()
        optimizer.zero_grad(set_to_none=True)
    return count, total_loss, False


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
        payload = load_polygon_checkpoint(
            resume, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True,
            expected_config=config,
        )
        start_epoch, history, saved_state = restore_committed_state(payload, "bc")
        best_nll = float(saved_state["bestNll"])
        stale = int(saved_state["stale"])
    status = "complete"
    for epoch in range(start_epoch, epochs):
        if time.monotonic() >= deadline:
            status = "budget_exhausted"
            break
        # Эта точка является единственным согласованным состоянием эпохи.
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch, config=config,
                                training_state=_bc_training_state(budget, epoch, best_nll, stale, history))
        samples, total_loss, exhausted = _train_polygon_bc_epoch(
            model,
            train_samples,
            optimizer,
            accumulation=accumulation,
            seed=config["seed"],
            epoch=epoch,
            device=device,
            deadline=deadline,
        )
        if exhausted:
            # Частично обновлённые веса и накопленные градиенты нельзя повторить с начала эпохи.
            load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch, config=config,
                                    training_state=_bc_training_state(budget, epoch, best_nll, stale, history))
            status = "budget_exhausted"
            break
        scheduler.step()
        nll = _validation_nll(model, validation_samples, device, deadline)
        if nll is None or time.monotonic() >= deadline:
            load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch, config=config,
                                    training_state=_bc_training_state(budget, epoch, best_nll, stale, history))
            status = "budget_exhausted"
            break
        history.append({"stage": "bc", "epoch": epoch + 1, "samples": samples,
                        "trainingLoss": total_loss / max(samples, 1), "validationNll": nll})
        if nll < best_nll:
            best_nll = nll
            stale = 0
            save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config,
                                    training_state=_bc_training_state(budget, epoch + 1, best_nll, stale, history))
        else:
            stale += 1
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config,
                                training_state=_bc_training_state(budget, epoch + 1, best_nll, stale, history))
        if stale >= settings["earlyStoppingPatience"]:
            break
    if not best_path.exists():
        completed = len(history)
        save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="bc", step=completed, config=config,
                                training_state=_bc_training_state(budget, completed, best_nll, stale, history))
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


def _optimize_polygon_ppo_update(
    model: HierarchicalPolygonPolicyV1,
    transitions: Sequence[PolygonPpoTransition],
    advantages: np.ndarray,
    returns: np.ndarray,
    optimizer: torch.optim.Optimizer,
    settings: Mapping[str, Any],
    *,
    epochs: int,
    entropy: float,
    seed: int,
    update: int,
    device: torch.device,
    deadline: float,
) -> tuple[float, bool]:
    """Выполняет градиентные шаги одного полигонального обновления PPO."""

    model.train()
    total_loss = 0.0
    for epoch in range(epochs):
        indices = list(range(len(transitions)))
        random.Random(seed + update * 1009 + epoch).shuffle(indices)
        optimizer.zero_grad(set_to_none=True)
        for local, index in enumerate(indices, 1):
            if time.monotonic() >= deadline:
                return total_loss, True
            item = transitions[index]
            decision = evaluate_polygon_components(
                model,
                item.fixed,
                item.dynamic,
                item.placement,
                item.instance,
                item.rotation,
                item.position,
                device,
            )
            if time.monotonic() >= deadline:
                return total_loss, True
            ratio = torch.exp(
                decision.log_probability - torch.tensor(item.old_log_probability, device=device)
            )
            advantage = torch.tensor(float(advantages[index]), device=device)
            actor = -torch.minimum(
                ratio * advantage,
                torch.clamp(
                    ratio, 1 - settings["clipRatio"], 1 + settings["clipRatio"]
                )
                * advantage,
            )
            value = torch.square(
                decision.value - torch.tensor(float(returns[index]), device=device)
            )
            loss = actor + settings["valueCoefficient"] * value - entropy * decision.entropy
            if time.monotonic() >= deadline:
                return total_loss, True
            (loss / 64).backward()
            total_loss += float(loss.detach())
            if local % 64 == 0 or local == len(indices):
                if time.monotonic() >= deadline:
                    return total_loss, True
                torch.nn.utils.clip_grad_norm_(model.parameters(), settings["maxGradientNorm"])
                optimizer.step()
                optimizer.zero_grad(set_to_none=True)
    return total_loss, False


def train_polygon_ppo(model: HierarchicalPolygonPolicyV1, train_episodes: Sequence[PolygonExpertEpisode],
                      validation_episodes: Sequence[PolygonExpertEpisode], validation_baselines: Mapping[str, Any],
                      config: Mapping[str, Any], run_dir: Path, device: torch.device, *, smoke: bool,
                      deadline: float, resume: str | Path | None = None,
                      budget: PolygonTrainingBudget | None = None,
                      prior_history: Sequence[Mapping[str, Any]] = ()) -> tuple[Path, list[dict[str, Any]], str]:
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
    history: list[dict[str, Any]] = [dict(item) for item in prior_history]
    if resume is not None:
        payload = load_polygon_checkpoint(
            resume, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True,
            expected_config=config,
        )
        start, history, saved_state = restore_committed_state(payload, "ppo")
    stored_score = payload.get("trainingState", {}).get("bestScore") if resume is not None else None
    best_score: tuple[float, ...] | None = tuple(stored_score) if stored_score is not None else None
    status = "complete"
    committed_update = start
    interval = 1 if smoke else max(1, updates // 10)
    for update in range(start, updates):
        if time.monotonic() >= deadline:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_ppo_training_state(budget, update, best_score, history))
            status = "budget_exhausted"; break
        transitions = _collect_transitions(model, train_episodes, count, device, config["seed"] + update,
                                           settings["workers"], deadline)
        if len(transitions) != count:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_ppo_training_state(budget, update, best_score, history))
            status = "budget_exhausted"; break
        # Файл содержит только согласованное состояние до текущего обновления.
        # При истечении времени частично применённые градиенты будут отброшены.
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                training_state=_ppo_training_state(budget, update, best_score, history))
        if time.monotonic() >= deadline:
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_ppo_training_state(budget, update, best_score, history))
            status = "budget_exhausted"; break
        advantages, returns = _advantages(transitions, settings["gamma"], settings["gaeLambda"])
        advantages = (advantages - advantages.mean()) / max(float(advantages.std()), 1e-8)
        entropy = settings["entropyStart"] + (settings["entropyEnd"] - settings["entropyStart"]) * update / max(updates - 1, 1)
        total_loss, exhausted = _optimize_polygon_ppo_update(
            model,
            transitions,
            advantages,
            returns,
            optimizer,
            settings,
            epochs=epochs,
            entropy=entropy,
            seed=config["seed"],
            update=update,
            device=device,
            deadline=deadline,
        )
        if exhausted or time.monotonic() >= deadline:
            load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
            save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                    training_state=_ppo_training_state(budget, update, best_score, history))
            status = "budget_exhausted"
            break
        scheduler.step()
        record: dict[str, Any] = {"stage": "ppo", "update": update + 1, "transitions": count,
                                  "loss": total_loss / max(count * epochs, 1), "entropyCoefficient": entropy}
        improved = False
        if (update + 1) % interval == 0 or update + 1 == updates:
            validation = _validation_score(model, validation_episodes, validation_baselines, device, config["seed"], deadline)
            if validation is None:
                load_polygon_checkpoint(resume_path, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
                save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update, config=config,
                                        training_state=_ppo_training_state(budget, update, best_score, history))
                status = "budget_exhausted"; break
            score, details = validation; record["validation"] = details
            if best_score is None or score > best_score:
                best_score = score
                improved = True
        history.append(record)
        committed_update = update + 1
        if improved:
            save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="ppo", step=committed_update, config=config,
                                    training_state=_ppo_training_state(budget, committed_update, best_score, history))
        save_polygon_checkpoint(resume_path, model, optimizer, scheduler, stage="ppo", step=update + 1, config=config,
                                training_state=_ppo_training_state(budget, committed_update, best_score, history))
    if not best_path.exists():
        save_polygon_checkpoint(best_path, model, optimizer, scheduler, stage="ppo", step=committed_update, config=config,
                                training_state=_ppo_training_state(budget, committed_update, best_score, history))
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
    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"]).to(device)
    resume_payload = load_polygon_checkpoint(resume, model, device, expected_config=config) if resume is not None else None
    write_canonical_json(output / "training-config.json", config)
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
    run_history: list[dict[str, Any]] = []
    ppo_resume = None
    if resume is None:
        bc_path, bc_history, bc_status = train_polygon_bc(model, cached["train"], cached["validation"], config, output, device,
                                                         smoke=smoke, deadline=deadline, budget=budget)
        if bc_status != "complete":
            ppo_path = bc_path; ppo_history = []; status = bc_status
        else:
            ppo_path, ppo_history, status = train_polygon_ppo(model, train, validation, baselines, config, output,
                                                             device, smoke=smoke, deadline=deadline, budget=budget,
                                                             prior_history=bc_history)
            run_history = ppo_history
        if bc_status != "complete":
            run_history = bc_history
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
                                                             budget=budget, prior_history=bc_history)
            run_history = ppo_history
        else:
            ppo_path = bc_path; ppo_history = []; status = bc_status
            run_history = bc_history
    metrics_path = output / "metrics.jsonl"
    write_metrics_history(metrics_path, run_history)
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
