#include <gtest/gtest.h>

#include "../../../src/core/app/services/io/packingsceneloader.h"
#include "../../../src/core/app/services/io/packingscenesaver.h"

//------------------------------------------------------------------------------
/**
  Проверяет сохранение пустой сцены в JSON формат
*/
//--
TEST(PackingSceneSaver, SavesEmptySceneAsJson)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;

  const std::string text = PackingSceneSaver::saveToText(snapshot);

  EXPECT_NE(text.find("\"format\": \"aipackaging.packing_scene\""), std::string::npos);
  EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
  EXPECT_NE(text.find("\"objects\": ["), std::string::npos);
  EXPECT_NE(text.find("\"actions\": ["), std::string::npos);
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение размера доски
*/
//--
TEST(PackingSceneSaver, SavesBoardSize)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 15;

  const std::string text = PackingSceneSaver::saveToText(snapshot);

  EXPECT_NE(text.find("\"columns\": 20"), std::string::npos);
  EXPECT_NE(text.find("\"rows\": 15"), std::string::npos);
  EXPECT_NE(text.find("\"unit\": \"cell\""), std::string::npos);
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение объектов и действий размещения
*/
//--
TEST(PackingSceneSaver, SavesObjectsAndPlaceActions)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}, {1, 0}, {1, 1}}});
  snapshot.actions.push_back({"place", "object_0", 5.0, 3.0, 0.0});

  const std::string text = PackingSceneSaver::saveToText(snapshot);

  EXPECT_NE(text.find("\"id\": \"object_0\""), std::string::npos);
  EXPECT_NE(text.find("\"type\": \"cell_grid\""), std::string::npos);
  EXPECT_NE(text.find("{ \"column\": 0, \"row\": 0 }"), std::string::npos);
  EXPECT_NE(text.find("{ \"column\": 1, \"row\": 1 }"), std::string::npos);
  EXPECT_NE(text.find("\"type\": \"place\""), std::string::npos);
  EXPECT_NE(text.find("\"objectId\": \"object_0\""), std::string::npos);
  EXPECT_NE(text.find("\"x\": 5.0"), std::string::npos);
  EXPECT_NE(text.find("\"y\": 3.0"), std::string::npos);
  EXPECT_NE(text.find("\"rotationDegrees\": 0.0"), std::string::npos);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что JSON сцены не использует OBJECT формат пула
*/
//--
TEST(PackingSceneSaver, DoesNotSaveObjectPoolFormat)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 0.0});

  const std::string text = PackingSceneSaver::saveToText(snapshot);

  EXPECT_EQ(text.find("OBJECT\n"), std::string::npos);
  EXPECT_EQ(text.find("END_OBJECT"), std::string::npos);
}


//------------------------------------------------------------------------------
/**
  Проверяет двустороннее преобразование сцены JSON через модуль сохранения и загрузчик
*/
//--
TEST(PackingSceneLoader, LoadsValidRoundTripFromSaver)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}, {1, 0}}});
  snapshot.actions.push_back({"place", "object_0", 3.0, 4.0, 0.0});

  const auto result = PackingSceneLoader::loadFromText(PackingSceneSaver::saveToText(snapshot));

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.snapshot.objects.size(), 1);
  ASSERT_EQ(result.snapshot.actions.size(), 1);
  EXPECT_EQ(result.snapshot.board.columns, 20);
  EXPECT_EQ(result.snapshot.board.rows, 20);
  EXPECT_EQ(result.snapshot.actions.front().objectId, "object_0");
  EXPECT_EQ(result.snapshot.actions.front().x, 3.0);
  EXPECT_EQ(result.snapshot.actions.front().y, 4.0);
}


//------------------------------------------------------------------------------
/**
  Проверяет загрузку пустой JSON-сцены
*/
//--
TEST(PackingSceneLoader, LoadsEmptyScene)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;

  const auto result = PackingSceneLoader::loadFromText(PackingSceneSaver::saveToText(snapshot));

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.snapshot.objects.empty());
  EXPECT_TRUE(result.snapshot.actions.empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при неверном формате сцены
*/
//--
TEST(PackingSceneLoader, RejectsInvalidFormat)
{
  const std::string text = "{\n"
                           "  \"format\": \"other\",\n"
                           "  \"version\": 1,\n"
                           "  \"board\": { \"columns\": 20, \"rows\": 20, \"unit\": \"cell\" },\n"
                           "  \"objects\": [],\n"
                           "  \"actions\": []\n"
                           "}\n";

  EXPECT_FALSE(PackingSceneLoader::loadFromText(text).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при неподдерживаемой версии
*/
//--
TEST(PackingSceneLoader, RejectsUnsupportedVersion)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  std::string text = PackingSceneSaver::saveToText(snapshot);
  const size_t versionPos = text.find("\"version\": 1");
  ASSERT_NE(versionPos, std::string::npos);
  text.replace(versionPos, std::string("\"version\": 1").size(), "\"version\": 2");

  EXPECT_FALSE(PackingSceneLoader::loadFromText(text).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при неподдерживаемом типе геометрии
*/
//--
TEST(PackingSceneLoader, RejectsUnsupportedGeometryType)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 0.0});
  std::string text = PackingSceneSaver::saveToText(snapshot);
  const size_t typePos = text.find("\"type\": \"cell_grid\"");
  ASSERT_NE(typePos, std::string::npos);
  text.replace(typePos, std::string("\"type\": \"cell_grid\"").size(), "\"type\": \"polygon\"");

  EXPECT_FALSE(PackingSceneLoader::loadFromText(text).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при ненулевом повороте
*/
//--
TEST(PackingSceneLoader, RejectsNonZeroRotation)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 90.0});

  EXPECT_FALSE(PackingSceneLoader::loadFromText(PackingSceneSaver::saveToText(snapshot)).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ, если действие ссылается на отсутствующий объект
*/
//--
TEST(PackingSceneLoader, RejectsActionReferencingMissingObject)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 0.0});

  EXPECT_FALSE(PackingSceneLoader::loadFromText(PackingSceneSaver::saveToText(snapshot)).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при некорректном JSON
*/
//--
TEST(PackingSceneLoader, RejectsMalformedJson)
{
  EXPECT_FALSE(PackingSceneLoader::loadFromText("{").success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при слишком большом размере доски
*/
//--
TEST(PackingSceneLoader, RejectsHugeBoardSize)
{
  const std::string text = "{\n"
                           "  \"format\": \"aipackaging.packing_scene\",\n"
                           "  \"version\": 1,\n"
                           "  \"board\": { \"columns\": 1e100, \"rows\": 20, \"unit\": \"cell\" },\n"
                           "  \"objects\": [],\n"
                           "  \"actions\": []\n"
                           "}\n";

  EXPECT_FALSE(PackingSceneLoader::loadFromText(text).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при некорректных координатах клеток объекта
*/
//--
TEST(PackingSceneLoader, RejectsInvalidCellCoordinates)
{
  const std::string hugeCellText =
    "{\n"
    "  \"format\": \"aipackaging.packing_scene\",\n"
    "  \"version\": 1,\n"
    "  \"board\": { \"columns\": 20, \"rows\": 20, \"unit\": \"cell\" },\n"
    "  \"objects\": [\n"
    "    { \"id\": \"object_0\", \"geometry\": { \"type\": \"cell_grid\", \"cells\": [ { \"column\": 1e100, \"row\": 0 } ] } }\n"
    "  ],\n"
    "  \"actions\": []\n"
    "}\n";
  const std::string fractionalCellText =
    "{\n"
    "  \"format\": \"aipackaging.packing_scene\",\n"
    "  \"version\": 1,\n"
    "  \"board\": { \"columns\": 20, \"rows\": 20, \"unit\": \"cell\" },\n"
    "  \"objects\": [\n"
    "    { \"id\": \"object_0\", \"geometry\": { \"type\": \"cell_grid\", \"cells\": [ { \"column\": 0.5, \"row\": 0 } ] } }\n"
    "  ],\n"
    "  \"actions\": []\n"
    "}\n";

  EXPECT_FALSE(PackingSceneLoader::loadFromText(hugeCellText).success);
  EXPECT_FALSE(PackingSceneLoader::loadFromText(fractionalCellText).success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при некорректных координатах действия размещения
*/
//--
TEST(PackingSceneLoader, RejectsInvalidActionCoordinates)
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 0.0});

  std::string hugeActionText = PackingSceneSaver::saveToText(snapshot);
  const size_t hugeXPos = hugeActionText.find("\"x\": 0.0");
  ASSERT_NE(hugeXPos, std::string::npos);
  hugeActionText.replace(hugeXPos, std::string("\"x\": 0.0").size(), "\"x\": 1e100");

  std::string fractionalActionText = PackingSceneSaver::saveToText(snapshot);
  const size_t fractionalXPos = fractionalActionText.find("\"x\": 0.0");
  ASSERT_NE(fractionalXPos, std::string::npos);
  fractionalActionText.replace(fractionalXPos, std::string("\"x\": 0.0").size(), "\"x\": 0.5");

  EXPECT_FALSE(PackingSceneLoader::loadFromText(hugeActionText).success);
  EXPECT_FALSE(PackingSceneLoader::loadFromText(fractionalActionText).success);
}
