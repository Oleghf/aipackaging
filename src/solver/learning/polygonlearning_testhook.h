#ifndef AIPACKAGING_SOLVER_LEARNING_POLYGONLEARNING_TESTHOOK_H
#define AIPACKAGING_SOLVER_LEARNING_POLYGONLEARNING_TESTHOOK_H

#include <cstdint>
#include <functional>

namespace aipackaging::solver::internal
{
/// Обозначает этап подготовки перехода для воспроизведения отказа в тесте.
enum class PolygonLearningStage : std::uint8_t
{
  Catalog,
  Observation,
  FullObservation
};

/// Устанавливает только для текущего потока закрытую функцию проверки отказов.
void setPolygonLearningFailureHook(std::function<void(PolygonLearningStage)> hook);
} // namespace aipackaging::solver::internal

#endif
