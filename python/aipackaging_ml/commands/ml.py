"""Ленивые обработчики CLI-команд обучения, оценки и экспорта модели."""

from __future__ import annotations

from argparse import Namespace
from typing import Any


def train(arguments: Namespace) -> dict[str, Any]:
    """Запускает BC/PPO только после явного выбора команды `train`."""

    from ..training import train_pipeline

    return train_pipeline(
        arguments.config,
        arguments.dataset,
        arguments.run_dir,
        device_name=arguments.device,
        smoke=arguments.smoke,
        resume=arguments.resume,
    )


def evaluate(arguments: Namespace) -> dict[str, Any]:
    """Оценивает контрольную точку после явного выбора команды `evaluate`."""

    from ..evaluation import evaluate_checkpoint

    return evaluate_checkpoint(
        arguments.checkpoint,
        arguments.config,
        arguments.dataset,
        arguments.output,
        split=arguments.split,
        device_name=arguments.device,
        smoke=arguments.smoke,
    )


def export_onnx(arguments: Namespace) -> dict[str, Any]:
    """Экспортирует комплект ONNX только после явного выбора команды `export-onnx`."""

    from ..exporting import export_policy_bundle

    return export_policy_bundle(
        arguments.checkpoint,
        arguments.config,
        arguments.output,
        model_id=arguments.model_id,
    )


def verify_model(arguments: Namespace) -> dict[str, Any]:
    """Проверяет комплект модели только после явного выбора команды `verify-model`."""

    from ..evaluation import verify_model_bundle

    return verify_model_bundle(
        arguments.model,
        arguments.checkpoint,
        arguments.config,
        arguments.dataset,
        split=arguments.split,
        smoke=arguments.smoke,
    )


def train_polygon(arguments: Namespace) -> dict[str, Any]:
    """Запускает полигональное BC/PPO после явного выбора команды."""

    from ..polygon_training import train_polygon_pipeline

    return train_polygon_pipeline(arguments.config, arguments.dataset, arguments.run_dir, device_name=arguments.device,
                                  smoke=arguments.smoke, resume=arguments.resume)


def evaluate_polygon(arguments: Namespace) -> dict[str, Any]:
    """Оценивает полигональную контрольную точку на замороженной выборке."""

    from ..polygon_evaluation import evaluate_polygon_checkpoint

    return evaluate_polygon_checkpoint(arguments.checkpoint, arguments.config, arguments.dataset, arguments.output,
                                       split=arguments.split, device_name=arguments.device, smoke=arguments.smoke)


def finalize_polygon_run(arguments: Namespace) -> dict[str, Any]:
    """Фиксирует остановленный запуск без продолжения обучения."""

    from ..polygon_run import finalize_polygon_run as finalize

    return finalize(
        arguments.config,
        arguments.dataset,
        arguments.run_dir,
        elapsed_training_seconds=arguments.elapsed_training_seconds,
        device_name=arguments.device,
    )


def verify_polygon_run(arguments: Namespace) -> dict[str, Any]:
    """Проверяет хеши и структуру файлов полигонального запуска."""

    from ..polygon_evaluation import verify_polygon_run as verify

    return verify(arguments.run_dir)


def export_polygon_onnx(arguments: Namespace) -> dict[str, Any]:
    """Экспортирует выбранную полигональную контрольную точку в ONNX."""

    from ..polygon_exporting import export_polygon_policy_bundle

    return export_polygon_policy_bundle(
        arguments.checkpoint, arguments.config, arguments.output, model_id=arguments.model_id
    )


def verify_polygon_model(arguments: Namespace) -> dict[str, Any]:
    """Проверяет хеши, оценки и жадные действия полигонального комплекта."""

    from ..polygon_exporting import load_polygon_model_metadata

    metadata = load_polygon_model_metadata(arguments.model)
    comparison = (arguments.checkpoint, arguments.config, arguments.dataset)
    if not any(comparison):
        return {
            "modelId": metadata["modelId"],
            "modelSha256": metadata["modelSha256"],
            "verified": True,
        }
    if not all(comparison):
        raise ValueError("численная проверка требует одновременно контрольную точку, конфигурацию и набор данных")

    from ..polygon_onnx_evaluation import verify_polygon_model_bundle

    return verify_polygon_model_bundle(
        arguments.model, arguments.checkpoint, arguments.config, arguments.dataset, smoke=arguments.smoke
    )


def evaluate_polygon_onnx(arguments: Namespace) -> dict[str, Any]:
    """Оценивает полигональный комплект ONNX по замороженным решениям."""

    from ..polygon_onnx_evaluation import evaluate_polygon_onnx as evaluate

    return evaluate(
        arguments.model, arguments.config, arguments.dataset, arguments.output,
        split=arguments.split, smoke=arguments.smoke,
    )
