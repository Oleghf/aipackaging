#ifndef AIPACKAGING_SUPPORT_UTF8PATH_H
#define AIPACKAGING_SUPPORT_UTF8PATH_H

#include <filesystem>
#include <string>
#include <string_view>

namespace aipackaging::files
{
/// Преобразует нейтральный путь UTF-8 в нативный путь; ошибка кодировки передаётся вызывающему адаптеру.
inline std::filesystem::path nativePath(std::string_view value)
{
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

/// Возвращает путь в UTF-8 независимо от системной кодовой страницы.
inline std::string utf8Path(const std::filesystem::path & value)
{
  const auto text = value.u8string();
  return {text.begin(), text.end()};
}
} // namespace aipackaging::files

#endif
