#include "atomicfile.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace aipackaging::solver::internal
{
namespace
{
std::atomic<std::uint64_t> nextTemporaryId{1};

/// Подбирает временное имя рядом с назначением, не затрагивая существующие файлы.
std::filesystem::path temporaryPath(const std::filesystem::path & destination)
{
  std::random_device random;
  const auto nonce = static_cast<std::uint64_t>(random()) << 32U | random();
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto id = nextTemporaryId.fetch_add(1, std::memory_order_relaxed);
  std::filesystem::path result = destination;
  result += ".aipackaging-" + std::to_string(nonce) + "-" + std::to_string(tick) + "-" + std::to_string(id) + ".tmp";
  return result;
}

/// Заменяет файл в пределах одного тома без копирования или предварительного удаления назначения.
void replaceFile(const std::filesystem::path & temporary, const std::filesystem::path & destination, bool exists,
                 std::string_view subject)
{
#ifdef _WIN32
  const BOOL replaced = exists ? ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)
                               : MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
  if (!replaced)
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                            "не удалось заменить файл " + std::string(subject));
#else
  (void)exists;
  std::filesystem::rename(temporary, destination);
#endif
}
} // namespace

/// Сначала завершает запись временного файла и только затем публикует новое содержимое.
bool writeFileAtomically(const std::filesystem::path & destination, const std::string & content, std::string & error,
                         const std::function<void(AtomicWriteStage)> & fault, std::string_view subject)
{
  std::filesystem::path temporary;
  try
  {
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(destination, statusError);
    const bool exists = !statusError && std::filesystem::exists(status);
    if (statusError && statusError != std::errc::no_such_file_or_directory)
      throw std::filesystem::filesystem_error("не удалось проверить файл " + std::string(subject), destination, statusError);
    if (exists && !std::filesystem::is_regular_file(status))
      throw std::invalid_argument("назначение " + std::string(subject) + " должно быть обычным файлом");

    // Случайный суффикс и проверка не позволяют обычным параллельным записям выбрать одно имя.
    for (int attempt = 0; attempt < 8; ++attempt)
    {
      temporary = temporaryPath(destination);
      if (!std::filesystem::exists(temporary))
        break;
      temporary.clear();
    }
    if (temporary.empty())
      throw std::runtime_error("не удалось создать уникальное имя временного файла");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("не удалось открыть временный файл " + std::string(subject));
    const auto middle = content.size() / 2;
    output.write(content.data(), static_cast<std::streamsize>(middle));
    if (!output)
      throw std::runtime_error("не удалось записать временный файл " + std::string(subject));
    if (fault)
      fault(AtomicWriteStage::AfterPartialWrite);
    output.write(content.data() + middle, static_cast<std::streamsize>(content.size() - middle));
    output.flush();
    if (!output)
      throw std::runtime_error("не удалось записать временный файл " + std::string(subject));
    output.close();
    if (!output)
      throw std::runtime_error("не удалось закрыть временный файл " + std::string(subject));
    if (fault)
      fault(AtomicWriteStage::BeforeReplace);
    replaceFile(temporary, destination, exists, subject);
    error.clear();
    return true;
  }
  catch (const std::exception & exception)
  {
    error = exception.what();
    if (!temporary.empty())
    {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    }
    return false;
  }
}
} // namespace aipackaging::solver::internal
