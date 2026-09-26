#include <filesystem>
#include <fstream>
#include <string>

#include <aipackaging/editor/polygon_document_validation.h>
#include <aipackaging/editor/polygon_draft_io.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace aipackaging::editor;

namespace
{
/// Создаёт прямоугольный контур, пригодный для проверки всех полей формата.
EditablePath rectangle(EditablePolygonDocument & document)
{
  EditablePath path;
  path.id = document.allocateEntityId();
  path.closed = true;
  for (const auto [x, y] : {std::pair{0.0, 0.0}, {20.0, 0.0}, {20.0, 10.0}, {0.0, 10.0}})
    path.vertices.push_back({document.allocateEntityId(), x, y});
  for (int index = 0; index < 4; ++index)
    path.segments.push_back({document.allocateEntityId(), EditableSegmentKind::Line});
  return path;
}

/// Создаёт канонический черновик с корректной редактируемой задачей.
PolygonDraft sampleDraft()
{
  PolygonDraft draft;
  draft.metadata.source = DraftSourceKind::ProblemFile;
  draft.metadata.sourceIdentifier = "example.json";
  draft.metadata.savedAtUtc = "2026-09-26T12:00:00Z";
  draft.metadata.generation = 7;
  draft.metadata.baseFingerprint = DraftFileFingerprint{123, 456};
  draft.document.problemId = "draft-problem";
  draft.document.sheet.width = 100.0;
  draft.document.sheet.height = 80.0;
  EditablePart part;
  part.id = draft.document.allocateEntityId();
  part.partId = "part";
  part.quantity = 2;
  part.allowedRotations = {0, 90};
  part.outer = rectangle(draft.document);
  draft.document.parts.push_back(std::move(part));
  return draft;
}

/// Предоставляет уникальный временный каталог и удаляет его после теста.
class TemporaryDirectory
{
public:
  /// Создаёт каталог внутри системной области временных файлов.
  TemporaryDirectory()
  {
    path = std::filesystem::temp_directory_path() /
           ("aipackaging-draft-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(path);
  }

  /// Удаляет только каталог, созданный этим объектом.
  ~TemporaryDirectory()
  {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }

  /// Запрещает копирование владельца одного временного каталога.
  TemporaryDirectory(const TemporaryDirectory &) = delete;
  /// Запрещает замену владельца одного временного каталога.
  TemporaryDirectory & operator=(const TemporaryDirectory &) = delete;

  std::filesystem::path path;
};
} // namespace

/// Проверяет детерминированный круговой проход через канонический JSON.
TEST(PolygonDraftIo, RoundTripProducesIdenticalCanonicalBytes)
{
  const PolygonDraft original = sampleDraft();
  std::string first;
  std::string error;
  ASSERT_TRUE(savePolygonDraftToText(original, first, error)) << error;
  PolygonDraft loaded;
  ASSERT_TRUE(loadPolygonDraftFromText(first, loaded, error)) << error;
  std::string second;
  ASSERT_TRUE(savePolygonDraftToText(loaded, second, error)) << error;
  EXPECT_EQ(first, second);
  EXPECT_EQ(loaded.document.nextEntityId(), original.document.nextEntityId());
  EXPECT_EQ(loaded.metadata.baseFingerprint, original.metadata.baseFingerprint);
}

/// Проверяет сохранение открытого временно некорректного контура в черновике.
TEST(PolygonDraftIo, PreservesLocallyInvalidEditableState)
{
  PolygonDraft draft = sampleDraft();
  draft.document.parts.front().outer->closed = false;
  draft.document.parts.front().outer->segments.pop_back();
  std::string text;
  std::string error;
  ASSERT_TRUE(savePolygonDraftToText(draft, text, error)) << error;
  PolygonDraft loaded;
  ASSERT_TRUE(loadPolygonDraftFromText(text, loaded, error)) << error;
  EXPECT_TRUE(hasDocumentErrors(validateEditableDocument(loaded.document)));
  EXPECT_FALSE(loaded.document.parts.front().outer->closed);
}

/// Проверяет отклонение неизвестного поля и неподдерживаемой версии.
TEST(PolygonDraftIo, RejectsUnknownFieldsAndVersions)
{
  std::string text;
  std::string error;
  ASSERT_TRUE(savePolygonDraftToText(sampleDraft(), text, error));
  nlohmann::json value = nlohmann::json::parse(text);
  value["unexpected"] = true;
  PolygonDraft loaded;
  EXPECT_FALSE(loadPolygonDraftFromText(value.dump(), loaded, error));
  value.erase("unexpected");
  value["version"] = 2;
  EXPECT_FALSE(loadPolygonDraftFromText(value.dump(), loaded, error));
}

/// Проверяет глобальную уникальность идентификаторов и монотонность их источника.
TEST(PolygonDraftIo, RejectsDuplicateAndNonAdvancingIdentifiers)
{
  std::string text;
  std::string error;
  ASSERT_TRUE(savePolygonDraftToText(sampleDraft(), text, error));
  nlohmann::json value = nlohmann::json::parse(text);
  value["document"]["parts"][0]["entityId"] = value["document"]["sheet"]["entityId"];
  PolygonDraft loaded;
  EXPECT_FALSE(loadPolygonDraftFromText(value.dump(), loaded, error));

  value = nlohmann::json::parse(text);
  value["nextEntityId"] = value["document"]["parts"][0]["entityId"];
  EXPECT_FALSE(loadPolygonDraftFromText(value.dump(), loaded, error));
}

/// Проверяет атомарную запись и отказ использовать каталог как файл назначения.
TEST(PolygonDraftIo, WritesAtomicallyAndRejectsDirectoryDestination)
{
  TemporaryDirectory temporary;
  const auto file = temporary.path / "document.aipdraft.json";
  std::string error;
  ASSERT_TRUE(savePolygonDraftToFile(file.string(), sampleDraft(), error)) << error;
  PolygonDraft loaded;
  ASSERT_TRUE(loadPolygonDraftFromFile(file.string(), loaded, error)) << error;

  const auto directory = temporary.path / "destination";
  std::filesystem::create_directory(directory);
  EXPECT_FALSE(savePolygonDraftToFile(directory.string(), sampleDraft(), error));
  EXPECT_TRUE(std::filesystem::is_directory(directory));
  std::size_t temporaryFiles = 0;
  for (const auto & entry : std::filesystem::directory_iterator(temporary.path))
    temporaryFiles += entry.path().extension() == ".tmp" ? 1U : 0U;
  EXPECT_EQ(temporaryFiles, 0U);
}
