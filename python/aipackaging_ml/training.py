"""Воспроизводимое обучение hierarchical policy через BC и PPO."""

from __future__ import annotations

import json
import math
import os
import random
import time
from dataclasses import replace
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np
import torch

from .contracts import canonical_json, load_training_config, sha256_file, write_canonical_json
from .model import HierarchicalGridPolicyV1
from .policy import PolicyRunner, encode_observation, evaluate_action, select_action
from .rl import PpoTransition, compute_gae
from .rollout import MultiprocessRolloutPool
from .training_data import ExpertEpisode, load_expert_episodes, replay_expert_steps


def configure_determinism(seed: int) -> None:
    """Настраивает Python, NumPy и PyTorch на воспроизводимый float32-запуск."""

    os.environ.setdefault("CUBLAS_WORKSPACE_CONFIG", ":4096:8")
    random.seed(seed)
    np.random.seed(seed % (2**32))
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    torch.backends.cudnn.benchmark = False
    torch.backends.cudnn.deterministic = True
    torch.use_deterministic_algorithms(True)


def _checkpoint_payload(
    model: HierarchicalGridPolicyV1,
    optimizer: torch.optim.Optimizer,
    scheduler: torch.optim.lr_scheduler.LRScheduler,
    *,
    stage: str,
    step: int,
    config: Mapping[str, Any],
) -> dict[str, Any]:
    """Собирает модель, optimizer и RNG state для точного продолжения запуска."""

    payload: dict[str, Any] = {
        "format": "aipackaging.training_checkpoint",
        "version": 1,
        "stage": stage,
        "step": step,
        "config": dict(config),
        "modelState": model.state_dict(),
        "optimizerState": optimizer.state_dict(),
        "schedulerState": scheduler.state_dict(),
        "pythonRandomState": random.getstate(),
        "numpyRandomState": np.random.get_state(),
        "torchRandomState": torch.get_rng_state(),
    }
    if torch.cuda.is_available():
        payload["cudaRandomState"] = torch.cuda.get_rng_state_all()
    return payload


def save_checkpoint(
    path: str | Path,
    model: HierarchicalGridPolicyV1,
    optimizer: torch.optim.Optimizer,
    scheduler: torch.optim.lr_scheduler.LRScheduler,
    *,
    stage: str,
    step: int,
    config: Mapping[str, Any],
) -> str:
    """Атомарно сохраняет доверенный локальный checkpoint и возвращает SHA-256."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    torch.save(_checkpoint_payload(model, optimizer, scheduler, stage=stage, step=step, config=config), temporary)
    temporary.replace(destination)
    return sha256_file(destination)


def load_checkpoint(
    path: str | Path,
    model: HierarchicalGridPolicyV1,
    device: torch.device,
    *,
    optimizer: torch.optim.Optimizer | None = None,
    scheduler: torch.optim.lr_scheduler.LRScheduler | None = None,
    restore_rng: bool = False,
) -> dict[str, Any]:
    """Загружает trusted checkpoint и опционально восстанавливает optimizer/RNG."""

    # Checkpoint содержит optimizer и RNG state, поэтому это внутренний trusted
    # artifact. Пользовательские .pt файлы этим API загружать нельзя.
    payload = torch.load(Path(path), map_location=device, weights_only=False)
    if payload.get("format") != "aipackaging.training_checkpoint" or payload.get("version") != 1:
        raise ValueError("unsupported training checkpoint")
    model.load_state_dict(payload["modelState"])
    if optimizer is not None:
        optimizer.load_state_dict(payload["optimizerState"])
    if scheduler is not None:
        scheduler.load_state_dict(payload["schedulerState"])
    if restore_rng:
        random.setstate(payload["pythonRandomState"])
        np.random.set_state(payload["numpyRandomState"])
        torch.set_rng_state(payload["torchRandomState"])
        if torch.cuda.is_available() and "cudaRandomState" in payload:
            torch.cuda.set_rng_state_all(payload["cudaRandomState"])
    return payload


def _validation_nll(
    model: HierarchicalGridPolicyV1,
    episodes: Sequence[ExpertEpisode],
    device: torch.device,
    deadline: float | None = None,
) -> float | None:
    """Возвращает средний NLL либо останавливается по общему wall-clock пределу."""

    model.eval()
    total = 0.0
    count = 0
    with torch.no_grad():
        for episode in episodes:
            for sample in replay_expert_steps(episode):
                if deadline is not None and time.monotonic() >= deadline:
                    return None
                total -= float(evaluate_action(model, sample.fixed, sample.dynamic, sample.action_index, device).log_probability)
                count += 1
    return total / max(count, 1)


def train_behavioral_cloning(
    model: HierarchicalGridPolicyV1,
    train_episodes: Sequence[ExpertEpisode],
    validation_episodes: Sequence[ExpertEpisode],
    config: Mapping[str, Any],
    run_dir: str | Path,
    device: torch.device,
    *,
    smoke: bool = False,
    deadline: float | None = None,
) -> tuple[Path, list[dict[str, Any]]]:
    """Обучает actor имитацией expert и critic точному оставшемуся return."""

    settings = config["behavioralCloning"]
    optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"], weight_decay=settings["weightDecay"])
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    epochs = 1 if smoke else settings["maxEpochs"]
    patience = settings["earlyStoppingPatience"]
    accumulation = settings["gradientAccumulation"]
    output = Path(run_dir) / "bc-best.pt"
    history: list[dict[str, Any]] = []
    best_nll = math.inf
    stale = 0
    exhausted = False

    for epoch in range(epochs):
        if deadline is not None and time.monotonic() >= deadline:
            exhausted = True
            break
        model.train()
        optimizer.zero_grad(set_to_none=True)
        order = list(range(len(train_episodes)))
        random.Random(config["seed"] + epoch).shuffle(order)
        training_loss = 0.0
        samples = 0
        for episode_index in order:
            for sample in replay_expert_steps(train_episodes[episode_index]):
                if deadline is not None and time.monotonic() >= deadline:
                    exhausted = True
                    break
                decision = evaluate_action(model, sample.fixed, sample.dynamic, sample.action_index, device)
                target = torch.tensor(sample.value_target, dtype=torch.float32, device=device)
                value_loss = torch.square(decision.value - target)
                loss = -decision.log_probability + 0.5 * value_loss
                (loss / accumulation).backward()
                training_loss += float(loss.detach())
                samples += 1
                if samples % accumulation == 0:
                    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                    optimizer.step()
                    optimizer.zero_grad(set_to_none=True)
            if exhausted:
                break
        if samples % accumulation:
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
        if samples:
            scheduler.step()

        validation_nll = _validation_nll(model, validation_episodes, device, deadline)
        if validation_nll is None:
            exhausted = True
            break
        record = {"stage": "bc", "epoch": epoch + 1, "samples": samples, "trainingLoss": training_loss / max(samples, 1), "validationNll": validation_nll}
        history.append(record)
        if validation_nll < best_nll:
            best_nll = validation_nll
            stale = 0
            save_checkpoint(output, model, optimizer, scheduler, stage="bc", step=epoch + 1, config=config)
        else:
            stale += 1
            if stale >= patience:
                break
    if not output.exists():
        save_checkpoint(output, model, optimizer, scheduler, stage="bc", step=len(history), config=config)
    load_checkpoint(output, model, device)
    return output, history


def _collect_transitions(
    model: HierarchicalGridPolicyV1,
    episodes: Sequence[ExpertEpisode],
    count: int,
    device: torch.device,
    seed: int,
    workers: int,
    deadline: float | None = None,
) -> list[PpoTransition]:
    """Собирает фиксированное число on-policy переходов по train-задачам."""

    model.eval()
    lane_results: list[list[PpoTransition]] = [[] for _ in range(min(workers, count))]
    collected = 0
    order = list(range(len(episodes)))
    random.Random(seed).shuffle(order)
    episode_cursor = 0
    generator = torch.Generator(device=device)
    generator.manual_seed(seed)
    lanes = min(workers, count)

    def next_tasks(number: int) -> list[tuple[Mapping[str, Any], int]]:
        """Выдаёт следующую детерминированную группу train-задач."""

        nonlocal episode_cursor
        tasks = []
        for _ in range(number):
            episode = episodes[order[episode_cursor % len(order)]]
            episode_cursor += 1
            tasks.append((episode.problem, seed + episode_cursor))
        return tasks

    with MultiprocessRolloutPool(lanes) as pool, torch.no_grad():
        states = pool.reset(next_tasks(lanes))
        while collected < count and (deadline is None or time.monotonic() < deadline):
            decisions = [select_action(model, fixed, dynamic, device, generator) for fixed, dynamic, _ in states]
            next_states = pool.step([decision.action_index for decision in decisions])
            reset_lanes: list[int] = []
            for lane, ((fixed, dynamic, _), decision, transition) in enumerate(zip(states, decisions, next_states, strict=True)):
                next_dynamic, reward, terminated, _, _ = transition
                next_value = 0.0 if terminated else float(encode_observation(model, fixed, next_dynamic, device).value)
                if collected < count:
                    lane_results[lane].append(
                        PpoTransition(
                            fixed,
                            dynamic,
                            decision.action_index,
                            float(decision.log_probability),
                            float(decision.value),
                            float(reward),
                            next_value,
                            terminated,
                        )
                    )
                    collected += 1
                if terminated:
                    reset_lanes.append(lane)
                else:
                    states[lane] = (fixed, next_dynamic, {})

            # Pipe API требует синхронной команды каждому процессу. Терминальные
            # lanes перезапускаются группой, а остальные сохраняют своё состояние.
            if reset_lanes and collected < count:
                replacements = {lane: next_tasks(1)[0] for lane in reset_lanes}
                for lane, state in pool.reset_lanes(replacements).items():
                    states[lane] = state
    # Трассы разных worker нельзя склеивать при обратном проходе GAE. Последняя
    # запись каждой lane помечается границей, сохраняя critic bootstrap next_value.
    for lane in lane_results:
        if lane:
            lane[-1] = replace(lane[-1], trace_end=True)
    return [transition for lane in lane_results for transition in lane]


def _validation_score(
    model: HierarchicalGridPolicyV1,
    episodes: Sequence[ExpertEpisode],
    device: torch.device,
    seed: int,
    deadline: float | None = None,
) -> tuple[tuple[float, ...], dict[str, Any]] | None:
    """Оценивает greedy policy либо останавливается по общему wall-clock пределу."""

    runner = PolicyRunner(model, model_id="validation", model_sha256="0" * 64, device=device)
    solutions = []
    for episode in episodes:
        if deadline is not None and time.monotonic() >= deadline:
            return None
        solutions.append(runner.solve(episode.problem, mode="greedy", seed=seed))
    solved = sum(solution["status"] == "solved" for solution in solutions)
    denominator = max(solved, 1)
    mean_used = sum(solution["objective"]["usedLength"] for solution in solutions if solution["status"] == "solved") / denominator
    mean_extra = sum(solution["objective"]["largestExtraRectangleArea"] for solution in solutions if solution["status"] == "solved") / denominator
    mean_fragmentation = sum(solution["objective"]["fragmentationPenalty"] for solution in solutions if solution["status"] == "solved") / denominator
    score = (float(solved), -mean_used, mean_extra, -mean_fragmentation)
    return score, {"solved": solved, "tasks": len(solutions), "meanUsedLength": mean_used, "meanLargestExtraRectangleArea": mean_extra, "meanFragmentationPenalty": mean_fragmentation}


def train_ppo(
    model: HierarchicalGridPolicyV1,
    train_episodes: Sequence[ExpertEpisode],
    validation_episodes: Sequence[ExpertEpisode],
    config: Mapping[str, Any],
    run_dir: str | Path,
    device: torch.device,
    *,
    smoke: bool = False,
    resume: str | Path | None = None,
    deadline: float | None = None,
) -> tuple[Path, list[dict[str, Any]], str]:
    """Дообучает BC-policy clipped PPO строго по reward v1 среды."""

    settings = config["ppo"]
    optimizer = torch.optim.AdamW(model.parameters(), lr=settings["learningRate"])
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    updates = 1 if smoke else settings["updates"]
    transitions_per_update = min(32, settings["transitionsPerUpdate"]) if smoke else settings["transitionsPerUpdate"]
    epochs = 1 if smoke else settings["epochsPerUpdate"]
    output = Path(run_dir) / "ppo-best.pt"
    history: list[dict[str, Any]] = []
    best_score: tuple[float, ...] | None = None
    started = time.monotonic()
    effective_deadline = deadline if deadline is not None else started + settings["maxWallTimeSeconds"]
    status = "complete"
    validation_interval = 1 if smoke else max(1, updates // 10)
    start_update = 0
    if resume is not None:
        payload = load_checkpoint(resume, model, device, optimizer=optimizer, scheduler=scheduler, restore_rng=True)
        if payload["stage"] != "ppo":
            raise ValueError("PPO resume requires a PPO checkpoint")
        start_update = int(payload["step"])

    for update in range(start_update, updates):
        if time.monotonic() >= effective_deadline:
            status = "budget_exhausted"
            break
        transitions = _collect_transitions(
            model,
            train_episodes,
            transitions_per_update,
            device,
            config["seed"] + update,
            settings["workers"],
            effective_deadline,
        )
        if len(transitions) != transitions_per_update:
            status = "budget_exhausted"
            break
        advantages, returns = compute_gae(transitions, settings["gamma"], settings["gaeLambda"])
        advantages = (advantages - advantages.mean()) / max(float(advantages.std()), 1e-8)
        entropy_coefficient = settings["entropyStart"] + (settings["entropyEnd"] - settings["entropyStart"]) * update / max(updates - 1, 1)

        model.train()
        update_loss = 0.0
        optimizer.zero_grad(set_to_none=True)
        optimizer_steps = 0
        for epoch in range(epochs):
            indices = list(range(len(transitions)))
            random.Random(config["seed"] + update * 1009 + epoch).shuffle(indices)
            for local_index, index in enumerate(indices, 1):
                if time.monotonic() >= effective_deadline:
                    status = "budget_exhausted"
                    break
                transition = transitions[index]
                decision = evaluate_action(model, transition.fixed, transition.dynamic, transition.action_index, device)
                old_log_probability = torch.tensor(transition.old_log_probability, dtype=torch.float32, device=device)
                advantage = torch.tensor(float(advantages[index]), dtype=torch.float32, device=device)
                target_return = torch.tensor(float(returns[index]), dtype=torch.float32, device=device)
                ratio = torch.exp(decision.log_probability - old_log_probability)
                unclipped = ratio * advantage
                clipped = torch.clamp(ratio, 1.0 - settings["clipRatio"], 1.0 + settings["clipRatio"]) * advantage
                actor_loss = -torch.minimum(unclipped, clipped)
                value_loss = torch.square(decision.value - target_return)
                loss = actor_loss + settings["valueCoefficient"] * value_loss - entropy_coefficient * decision.entropy
                (loss / 64).backward()
                update_loss += float(loss.detach())
                if local_index % 64 == 0 or local_index == len(indices):
                    torch.nn.utils.clip_grad_norm_(model.parameters(), settings["maxGradientNorm"])
                    optimizer.step()
                    optimizer.zero_grad(set_to_none=True)
                    optimizer_steps += 1
            if status == "budget_exhausted":
                break
        if status == "budget_exhausted":
            break
        scheduler.step()

        record: dict[str, Any] = {
            "stage": "ppo",
            "update": update + 1,
            "transitions": len(transitions),
            "optimizerSteps": optimizer_steps,
            "loss": update_loss / max(len(transitions) * epochs, 1),
            "entropyCoefficient": entropy_coefficient,
        }
        if (update + 1) % validation_interval == 0 or update + 1 == updates:
            validation_result = _validation_score(
                model, validation_episodes, device, config["seed"], effective_deadline
            )
            if validation_result is None:
                status = "budget_exhausted"
                break
            score, validation = validation_result
            record["validation"] = validation
            if best_score is None or score > best_score:
                best_score = score
                save_checkpoint(output, model, optimizer, scheduler, stage="ppo", step=update + 1, config=config)
        history.append(record)

    if not output.exists():
        save_checkpoint(output, model, optimizer, scheduler, stage="ppo", step=start_update + len(history), config=config)
    load_checkpoint(output, model, device)
    return output, history, status


def train_pipeline(
    config_path: str | Path,
    dataset_root: str | Path,
    run_dir: str | Path,
    *,
    device_name: str = "cuda",
    smoke: bool = False,
    resume: str | Path | None = None,
) -> dict[str, Any]:
    """Выполняет полный BC → PPO запуск и записывает аудируемый run manifest."""

    config = load_training_config(config_path)
    device = torch.device(device_name)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("canonical M3 training requires an available CUDA device")
    configure_determinism(config["seed"])
    deadline = time.monotonic() + config["ppo"]["maxWallTimeSeconds"]
    run_path = Path(run_dir)
    run_path.mkdir(parents=True, exist_ok=True)
    write_canonical_json(run_path / "training-config.json", config)
    train_episodes = load_expert_episodes(dataset_root, "train", expected_manifest_sha256=config["datasetManifestSha256"])
    validation_episodes = load_expert_episodes(dataset_root, "validation", expected_manifest_sha256=config["datasetManifestSha256"])
    if smoke:
        train_episodes = train_episodes[:2]
        validation_episodes = validation_episodes[:2]

    model = HierarchicalGridPolicyV1(config["model"]["hiddenSize"]).to(device)
    bc_history: list[dict[str, Any]] = []
    ppo_resume: str | Path | None = None
    if resume is None:
        bc_path, bc_history = train_behavioral_cloning(
            model,
            train_episodes,
            validation_episodes,
            config,
            run_path,
            device,
            smoke=smoke,
            deadline=deadline,
        )
    else:
        payload = load_checkpoint(resume, model, device)
        existing_bc = Path(resume).parent / "bc-best.pt"
        bc_path = existing_bc if existing_bc.exists() else Path(resume)
        if payload["stage"] == "ppo":
            ppo_resume = resume
    ppo_path, ppo_history, status = train_ppo(
        model,
        train_episodes,
        validation_episodes,
        config,
        run_path,
        device,
        smoke=smoke,
        resume=ppo_resume,
        deadline=deadline,
    )

    metrics_path = run_path / "metrics.jsonl"
    metrics_path.write_text("".join(canonical_json(item) + "\n" for item in (*bc_history, *ppo_history)), encoding="utf-8", newline="\n")
    manifest = {
        "format": "aipackaging.training_run",
        "version": 1,
        "status": status,
        "seed": config["seed"],
        "device": str(device),
        "datasetManifestSha256": config["datasetManifestSha256"],
        "configSha256": sha256_file(config_path),
        "bcCheckpoint": {"path": bc_path.name, "sha256": sha256_file(bc_path)},
        "ppoCheckpoint": {"path": ppo_path.name, "sha256": sha256_file(ppo_path)},
        "metrics": {"path": metrics_path.name, "sha256": sha256_file(metrics_path)},
        "software": {"python": os.sys.version.split()[0], "torch": torch.__version__, "cuda": torch.version.cuda or "none"},
    }
    write_canonical_json(run_path / "run-manifest.json", manifest)
    return manifest
