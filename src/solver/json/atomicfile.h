#ifndef AIPACKAGING_SOLVER_JSON_ATOMICFILE_H
#define AIPACKAGING_SOLVER_JSON_ATOMICFILE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace aipackaging::solver::internal
{
/// Обозначает границы записи, в которых тест может воспроизвести отказ накопителя.
enum class AtomicWriteStage : std::uint8_t
{
  AfterPartialWrite,
  BeforeReplace
};

/// Записывает содержимое во временный файл и заменяет назначение только после успешного закрытия.
bool writeFileAtomically(const std::filesystem::path & destination, const std::string & content, std::string & error,
                         const std::function<void(AtomicWriteStage)> & fault = {}, std::string_view subject = "решения");
} // namespace aipackaging::solver::internal

#endif
