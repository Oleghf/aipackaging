#ifndef AIPACKAGING_EDITOR_POLYGON_DRAFT_IO_H
#define AIPACKAGING_EDITOR_POLYGON_DRAFT_IO_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <aipackaging/editor/polygon_document.h>

namespace aipackaging::editor
{
/// Обозначает происхождение редактируемого документа, сохранённое в черновике.
enum class DraftSourceKind : std::uint8_t
{
  ProblemFile,
  DraftFile,
  Imported,
  Recovered,
  Untitled
};

/// Фиксирует наблюдавшиеся размер и время изменения исходного файла.
struct DraftFileFingerprint
{
  std::uint64_t size = 0;
  std::int64_t modifiedUnixNs = 0;

  /// Сравнивает оба компонента отпечатка исходного файла.
  bool operator==(const DraftFileFingerprint &) const = default;
};

/// Хранит сведения о происхождении и поколении одного сохранённого черновика.
struct PolygonDraftMetadata
{
  DraftSourceKind source = DraftSourceKind::Untitled;
  std::string sourceIdentifier;
  std::string savedAtUtc;
  std::uint64_t generation = 0;
  std::optional<DraftFileFingerprint> baseFingerprint;
};

/// Объединяет редактируемый документ и служебные сведения его локального черновика.
struct PolygonDraft
{
  PolygonDraftMetadata metadata;
  EditablePolygonDocument document;
};

/// Преобразует происхождение черновика в устойчивый литерал формата обмена.
std::string_view toString(DraftSourceKind source) noexcept;
/// Разбирает устойчивый литерал происхождения без изменения результата при ошибке.
bool parseDraftSourceKind(std::string_view text, DraftSourceKind & source) noexcept;
/// Разбирает строгий черновик, сохраняя допустимые временно некорректные состояния документа.
bool loadPolygonDraftFromText(const std::string & text, PolygonDraft & draft, std::string & error);
/// Загружает строгий черновик из файла целиком.
bool loadPolygonDraftFromFile(const std::string & filePath, PolygonDraft & draft, std::string & error);
/// Сериализует черновик в канонический JSON с завершающим переводом строки.
bool savePolygonDraftToText(const PolygonDraft & draft, std::string & text, std::string & error);
/// Атомарно записывает черновик, не повреждая прежний файл при отказе.
bool savePolygonDraftToFile(const std::string & filePath, const PolygonDraft & draft, std::string & error);
} // namespace aipackaging::editor

#endif
