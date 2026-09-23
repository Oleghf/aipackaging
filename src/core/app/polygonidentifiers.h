#ifndef AIPACKAGING_APPLICATION_POLYGONIDENTIFIERS_H
#define AIPACKAGING_APPLICATION_POLYGONIDENTIFIERS_H

#include <cstdint>

/// Непостоянный идентификатор загруженной полигональной задачи в текущем процессе.
struct PolygonDocumentHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный объект.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonDocumentHandle &, const PolygonDocumentHandle &) = default;
};

/// Непостоянный идентификатор проверенного решения в текущем процессе.
struct PolygonSolutionHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный объект.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonSolutionHandle &, const PolygonSolutionHandle &) = default;
};

/// Непостоянный идентификатор проверенного комплекта модели в текущем процессе.
struct PolygonModelHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированную модель.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonModelHandle &, const PolygonModelHandle &) = default;
};

/// Непостоянный идентификатор фонового запуска в текущем процессе.
struct NestingJobHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный запуск.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const NestingJobHandle &, const NestingJobHandle &) = default;
};

#endif
