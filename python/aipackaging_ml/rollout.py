"""Многопроцессное выполнение нативных сред для сбора траекторий политик."""

from __future__ import annotations

import multiprocessing
from multiprocessing.connection import Connection
from typing import Any, Mapping, Sequence


def _worker(connection: Connection, environment_kind: str) -> None:
    """Обслуживает одну нативную среду и возвращает ошибки главному процессу."""

    from .environment import GridNestingEnv, PolygonNestingEnv

    environment: GridNestingEnv | PolygonNestingEnv | None = None
    try:
        while True:
            command, payload = connection.recv()
            if command == "close":
                return
            try:
                if command == "reset":
                    environment_type = GridNestingEnv if environment_kind == "grid" else PolygonNestingEnv
                    if environment_kind == "polygon":
                        environment = environment_type.from_dict(payload["problem"], reward_version=2, catalog_version=1)
                    else:
                        environment = environment_type.from_dict(payload["problem"])
                    fixed = environment.static_observation()
                    dynamic, info = environment.reset_compact(seed=payload["seed"])
                    connection.send((True, (fixed, dynamic, info)))
                elif command == "step":
                    if environment is None:
                        raise RuntimeError("рабочий процесс траектории не был сброшен")
                    connection.send((True, environment.step_compact(payload)))
                elif command == "polygon-placement":
                    if environment_kind != "polygon" or environment is None:
                        raise RuntimeError("полигональная среда рабочего процесса не была сброшена")
                    connection.send(
                        (True, environment.placement_observation(payload["instance"], payload["rotationDegrees"]))
                    )
                elif command == "polygon-step-action":
                    if environment_kind != "polygon" or environment is None:
                        raise RuntimeError("полигональная среда рабочего процесса не была сброшена")
                    # Индекс динамического каталога вычисляется там же, где живёт среда:
                    # между запросом условного наблюдения и шагом состояние не меняется.
                    action_index = environment.find_action(payload)
                    connection.send((True, (action_index, environment.step_compact(action_index))))
                else:
                    raise ValueError(f"неизвестная команда рабочего процесса траектории: {command}")
            except Exception as error:  # noqa: BLE001 - ошибка должна перейти в главный процесс
                connection.send((False, f"{type(error).__name__}: {error}"))
    finally:
        connection.close()


class MultiprocessRolloutPool:
    """Параллельно выполняет геометрические переходы в фиксированном числе процессов."""

    def __init__(self, workers: int, *, environment_kind: str = "grid") -> None:
        """Создаёт одинаково запускаемые в Windows и Linux рабочие процессы."""

        if workers < 1:
            raise ValueError("число рабочих процессов траекторий должно быть положительным")
        if environment_kind not in {"grid", "polygon"}:
            raise ValueError("неизвестный вид среды рабочих процессов")
        context = multiprocessing.get_context("spawn")
        self._connections: list[Connection] = []
        self._processes: list[multiprocessing.Process] = []
        for _ in range(workers):
            parent, child = context.Pipe()
            process = context.Process(target=_worker, args=(child, environment_kind))
            process.start()
            child.close()
            self._connections.append(parent)
            self._processes.append(process)

    @property
    def workers(self) -> int:
        """Возвращает число активных независимых каналов сбора траекторий."""

        return len(self._connections)

    @staticmethod
    def _receive(connection: Connection) -> Any:
        """Возвращает данные рабочего процесса либо передаёт его ошибку."""

        success, payload = connection.recv()
        if not success:
            raise RuntimeError(f"рабочий процесс траектории завершился ошибкой: {payload}")
        return payload

    def reset(self, tasks: Sequence[tuple[Mapping[str, Any], int]]) -> list[tuple[dict[str, Any], dict[str, Any], dict[str, Any]]]:
        """Одновременно создаёт эпизод в каждом процессе и возвращает наблюдения."""

        if len(tasks) != self.workers:
            raise ValueError("число задач сброса должно совпадать с числом рабочих процессов")
        for connection, (problem, seed) in zip(self._connections, tasks, strict=True):
            connection.send(("reset", {"problem": dict(problem), "seed": seed}))
        return [self._receive(connection) for connection in self._connections]

    def step(self, actions: Sequence[int]) -> list[tuple[dict[str, Any], float, bool, bool, dict[str, Any]]]:
        """Одновременно применяет по одному индексу действия в каждом рабочем процессе."""

        if len(actions) != self.workers:
            raise ValueError("число действий должно совпадать с числом рабочих процессов")
        for connection, action in zip(self._connections, actions, strict=True):
            connection.send(("step", int(action)))
        return [self._receive(connection) for connection in self._connections]

    def polygon_placements(self, pairs: Sequence[tuple[int, int]]) -> list[dict[str, Any]]:
        """Параллельно получает условные наблюдения выбранных пар полигональной среды."""

        if len(pairs) != self.workers:
            raise ValueError("число пар должно совпадать с числом рабочих процессов")
        result = self.polygon_placements_lanes(dict(enumerate(pairs)))
        return [result[lane] for lane in range(self.workers)]

    def polygon_placements_lanes(self, pairs: Mapping[int, tuple[int, int]]) -> dict[int, dict[str, Any]]:
        """Получает условные наблюдения только перечисленных рабочих каналов."""

        for lane in sorted(pairs):
            if lane < 0 or lane >= self.workers:
                raise IndexError("индекс канала траектории находится вне допустимого диапазона")
        for lane, (instance, rotation) in sorted(pairs.items()):
            connection = self._connections[lane]
            connection.send(
                ("polygon-placement", {"instance": int(instance), "rotationDegrees": int(rotation) * 90})
            )
        return {lane: self._receive(self._connections[lane]) for lane in sorted(pairs)}

    def polygon_step_actions(
        self, actions: Sequence[Mapping[str, Any]]
    ) -> list[tuple[int, tuple[dict[str, Any], float, bool, bool, dict[str, Any]]]]:
        """Параллельно находит и применяет аудируемые полигональные действия."""

        if len(actions) != self.workers:
            raise ValueError("число действий должно совпадать с числом рабочих процессов")
        result = self.polygon_step_actions_lanes(dict(enumerate(actions)))
        return [result[lane] for lane in range(self.workers)]

    def polygon_step_actions_lanes(
        self, actions: Mapping[int, Mapping[str, Any]]
    ) -> dict[int, tuple[int, tuple[dict[str, Any], float, bool, bool, dict[str, Any]]]]:
        """Применяет действия только в перечисленных рабочих каналах."""

        for lane in sorted(actions):
            if lane < 0 or lane >= self.workers:
                raise IndexError("индекс канала траектории находится вне допустимого диапазона")
        for lane, action in sorted(actions.items()):
            connection = self._connections[lane]
            connection.send(("polygon-step-action", dict(action)))
        return {lane: self._receive(self._connections[lane]) for lane in sorted(actions)}

    def reset_lanes(
        self, tasks: Mapping[int, tuple[Mapping[str, Any], int]]
    ) -> dict[int, tuple[dict[str, Any], dict[str, Any], dict[str, Any]]]:
        """Сбрасывает только перечисленные завершённые каналы, сохраняя остальные."""

        for lane, (problem, seed) in sorted(tasks.items()):
            if lane < 0 or lane >= self.workers:
                raise IndexError("индекс канала траектории находится вне допустимого диапазона")
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
        """Возвращает пул для использования через диспетчер контекста."""

        return self

    def __exit__(self, *_: object) -> None:
        """Гарантированно закрывает все процессы при выходе из контекста."""

        self.close()
