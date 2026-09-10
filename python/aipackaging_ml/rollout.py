"""Multiprocessing-исполнение нативных сред для on-policy rollout M3."""

from __future__ import annotations

import multiprocessing
from multiprocessing.connection import Connection
from typing import Any, Mapping, Sequence


def _worker(connection: Connection) -> None:
    """Обслуживает одну нативную среду и возвращает ошибки главному процессу."""

    from .environment import GridNestingEnv

    environment: GridNestingEnv | None = None
    try:
        while True:
            command, payload = connection.recv()
            if command == "close":
                return
            try:
                if command == "reset":
                    environment = GridNestingEnv.from_dict(payload["problem"])
                    fixed = environment.static_observation()
                    dynamic, info = environment.reset_compact(seed=payload["seed"])
                    connection.send((True, (fixed, dynamic, info)))
                elif command == "step":
                    if environment is None:
                        raise RuntimeError("rollout worker has not been reset")
                    connection.send((True, environment.step_compact(payload)))
                else:
                    raise ValueError(f"unknown rollout worker command: {command}")
            except Exception as error:  # noqa: BLE001 - ошибка должна перейти в главный процесс
                connection.send((False, f"{type(error).__name__}: {error}"))
    finally:
        connection.close()


class MultiprocessRolloutPool:
    """Параллельно выполняет геометрические переходы в фиксированном числе процессов."""

    def __init__(self, workers: int) -> None:
        """Создаёт spawn-процессы, одинаковые на Windows и Linux."""

        if workers < 1:
            raise ValueError("rollout worker count must be positive")
        context = multiprocessing.get_context("spawn")
        self._connections: list[Connection] = []
        self._processes: list[multiprocessing.Process] = []
        for _ in range(workers):
            parent, child = context.Pipe()
            process = context.Process(target=_worker, args=(child,))
            process.start()
            child.close()
            self._connections.append(parent)
            self._processes.append(process)

    @property
    def workers(self) -> int:
        """Возвращает число активных независимых rollout lanes."""

        return len(self._connections)

    @staticmethod
    def _receive(connection: Connection) -> Any:
        """Возвращает worker payload либо поднимает переданную диагностику."""

        success, payload = connection.recv()
        if not success:
            raise RuntimeError(f"rollout worker failed: {payload}")
        return payload

    def reset(self, tasks: Sequence[tuple[Mapping[str, Any], int]]) -> list[tuple[dict[str, Any], dict[str, Any], dict[str, Any]]]:
        """Одновременно создаёт новый эпизод в каждом worker и возвращает observations."""

        if len(tasks) != self.workers:
            raise ValueError("reset task count must match rollout worker count")
        for connection, (problem, seed) in zip(self._connections, tasks, strict=True):
            connection.send(("reset", {"problem": dict(problem), "seed": seed}))
        return [self._receive(connection) for connection in self._connections]

    def step(self, actions: Sequence[int]) -> list[tuple[dict[str, Any], float, bool, bool, dict[str, Any]]]:
        """Одновременно применяет по одному action index в каждом worker."""

        if len(actions) != self.workers:
            raise ValueError("action count must match rollout worker count")
        for connection, action in zip(self._connections, actions, strict=True):
            connection.send(("step", int(action)))
        return [self._receive(connection) for connection in self._connections]

    def reset_lanes(
        self, tasks: Mapping[int, tuple[Mapping[str, Any], int]]
    ) -> dict[int, tuple[dict[str, Any], dict[str, Any], dict[str, Any]]]:
        """Сбрасывает только перечисленные terminal lanes, сохраняя остальные."""

        for lane, (problem, seed) in sorted(tasks.items()):
            if lane < 0 or lane >= self.workers:
                raise IndexError("rollout lane is out of range")
            self._connections[lane].send(("reset", {"problem": dict(problem), "seed": seed}))
        return {lane: self._receive(self._connections[lane]) for lane in sorted(tasks)}

    def close(self) -> None:
        """Штатно завершает процессы и освобождает IPC-каналы."""

        for connection in self._connections:
            try:
                connection.send(("close", None))
            except (BrokenPipeError, EOFError):
                pass
        for process in self._processes:
            process.join(timeout=5)
            if process.is_alive():
                process.terminate()
                process.join(timeout=5)
        for connection in self._connections:
            connection.close()
        self._connections.clear()
        self._processes.clear()

    def __enter__(self) -> "MultiprocessRolloutPool":
        """Возвращает pool для использования через context manager."""

        return self

    def __exit__(self, *_: object) -> None:
        """Гарантированно закрывает все процессы при выходе из контекста."""

        self.close()
