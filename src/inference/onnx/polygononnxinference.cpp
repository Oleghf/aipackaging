#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <aipackaging/inference/polygon_onnx.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_learning.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

namespace aipackaging::inference
{
using namespace aipackaging::solver;
namespace
{
using Json = nlohmann::json;
constexpr std::size_t SHEET_RASTER_SIZE = 128;
constexpr std::size_t SHAPE_RASTER_SIZE = 32;
constexpr std::uint64_t SPLITMIX_INCREMENT = 0x9e3779b97f4a7c15ULL;

/// Реализует SHA-256 без внешней криптографической зависимости настольного приложения.
class Sha256
{
public:
  /// Добавляет очередную последовательность байтов в вычисляемый хеш.
  void update(const unsigned char * data, std::size_t size)
  {
    totalBytes_ += size;
    while (size > 0)
    {
      const std::size_t copy = std::min(size, block_.size() - blockSize_);
      std::memcpy(block_.data() + blockSize_, data, copy);
      blockSize_ += copy;
      data += copy;
      size -= copy;
      if (blockSize_ == block_.size())
      {
        transform(block_.data());
        blockSize_ = 0;
      }
    }
  }

  /// Завершает дополнение сообщения и возвращает шестнадцатеричный SHA-256.
  std::string finish()
  {
    const std::uint64_t bits = totalBytes_ * 8;
    block_[blockSize_++] = 0x80;
    if (blockSize_ > 56)
    {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.end(), 0);
      transform(block_.data());
      blockSize_ = 0;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.begin() + 56, 0);
    for (int index = 0; index < 8; ++index)
      block_[63 - index] = static_cast<unsigned char>(bits >> (index * 8));
    transform(block_.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::uint32_t value : state_)
      output << std::setw(8) << value;
    return output.str();
  }

private:
  /// Выполняет один 64-байтный раунд SHA-256 над текущим состоянием.
  void transform(const unsigned char * data)
  {
    static constexpr std::array<std::uint32_t, 64> CONSTANTS = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index)
      words[index] = (static_cast<std::uint32_t>(data[index * 4]) << 24) |
                     (static_cast<std::uint32_t>(data[index * 4 + 1]) << 16) |
                     (static_cast<std::uint32_t>(data[index * 4 + 2]) << 8) | static_cast<std::uint32_t>(data[index * 4 + 3]);
    for (std::size_t index = 16; index < words.size(); ++index)
    {
      const std::uint32_t first = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
      const std::uint32_t second = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
      words[index] = words[index - 16] + first + words[index - 7] + second;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      const std::uint32_t sigma1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const std::uint32_t choice = (e & f) ^ (~e & g);
      const std::uint32_t temporary1 = h + sigma1 + choice + CONSTANTS[index] + words[index];
      const std::uint32_t sigma0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = sigma0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<unsigned char, 64> block_{};
  std::size_t blockSize_ = 0;
  std::uint64_t totalBytes_ = 0;
};

/// Вычисляет SHA-256 файла потоково, не загружая граф целиком в память.
std::string sha256File(const std::filesystem::path & path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Не удалось открыть файл комплекта модели: " + path.string());
  Sha256 sha;
  std::array<unsigned char, std::size_t{64} * 1024> buffer{};
  while (input)
  {
    input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    sha.update(buffer.data(), static_cast<std::size_t>(input.gcount()));
  }
  if (!input.eof())
    throw std::runtime_error("Не удалось прочитать файл комплекта модели: " + path.string());
  return sha.finish();
}

/// Вычисляет SHA-256 небольшой канонической строки метаданных.
std::string sha256Text(std::string_view value)
{
  Sha256 sha;
  sha.update(reinterpret_cast<const unsigned char *>(value.data()), value.size());
  return sha.finish();
}

/// Проверяет, что объект JSON содержит ровно ожидаемый набор полей.
bool exactFields(const Json & value, std::initializer_list<std::string_view> expected)
{
  if (!value.is_object() || value.size() != expected.size())
    return false;
  return std::ranges::all_of(expected, [&value](std::string_view name) { return value.contains(std::string(name)); });
}

/// Проверяет строковое представление SHA-256 в нижнем регистре.
bool isSha256(const Json & value)
{
  if (!value.is_string())
    return false;
  const std::string text = value.get<std::string>();
  return text.size() == 64 &&
         std::ranges::all_of(text, [](char character)
                             { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

/// Читает и строго проверяет метаданные комплекта до загрузки исполняемых графов.
PolygonModelMetadata loadMetadata(const std::filesystem::path & root)
{
  std::ifstream input(root / "metadata.json");
  if (!input)
    throw std::runtime_error("В каталоге модели отсутствует файл метаданных");
  Json metadata;
  try
  {
    input >> metadata;
  }
  catch (const Json::exception & exception)
  {
    throw std::runtime_error(std::string("Некорректный JSON метаданных модели: ") + exception.what());
  }
  if (!exactFields(metadata, {"format", "version", "modelId", "projectVersion", "architecture", "opset", "contracts", "limits",
                              "normalization", "selection", "dynamicAxes", "datasetManifestSha256", "trainingConfigSha256",
                              "checkpointSha256", "files", "modelSha256"}) ||
      metadata["format"] != "aipackaging.polygon_policy" || metadata["version"] != 2 ||
      metadata["architecture"] != "hierarchical-polygon-policy-v1" || metadata["opset"] != 23 ||
      metadata["normalization"] != "polygon-observation-v2" ||
      metadata["dynamicAxes"] != Json::array({"instances", "candidates"}))
    throw std::runtime_error("Метаданные полигональной модели имеют несовместимый формат");
  const Json expectedContracts = {{"problem", 1}, {"solution", 2}, {"observation", 2}, {"action", 1}, {"reward", 2}};
  const Json expectedSelection = {{"hierarchy", {"instance", "rotation", "position"}},
                                  {"tieBreak", "lowest-current-catalog-index"},
                                  {"sampling", "splitmix64-categorical-v1"}};
  if (metadata["contracts"] != expectedContracts || metadata["selection"] != expectedSelection)
    throw std::runtime_error("Версии контрактов или правила выбора модели несовместимы");
  const Json & limits = metadata["limits"];
  if (!exactFields(limits, {"hiddenSize", "sheetRasterSize", "shapeRasterSize", "maxInstances", "maxCandidatesPerPair"}) ||
      !limits["hiddenSize"].is_number_unsigned() || !limits["maxInstances"].is_number_unsigned() ||
      !limits["maxCandidatesPerPair"].is_number_unsigned() || limits["sheetRasterSize"] != 128 || limits["shapeRasterSize"] != 32)
    throw std::runtime_error("Ограничения полигональной модели несовместимы");
  const Json & files = metadata["files"];
  if (!exactFields(files, {"encoder.onnx", "placement-head.onnx"}) || !isSha256(files["encoder.onnx"]) ||
      !isSha256(files["placement-head.onnx"]) || !isSha256(metadata["datasetManifestSha256"]) ||
      !isSha256(metadata["trainingConfigSha256"]) || !isSha256(metadata["checkpointSha256"]) ||
      !isSha256(metadata["modelSha256"]))
    throw std::runtime_error("Метаданные полигональной модели содержат некорректный SHA-256");
  std::size_t fileCount = 0;
  for (const auto & entry : std::filesystem::directory_iterator(root))
  {
    ++fileCount;
    const std::string name = entry.path().filename().string();
    if (!entry.is_regular_file() || (name != "metadata.json" && name != "encoder.onnx" && name != "placement-head.onnx"))
      throw std::runtime_error("Каталог модели должен содержать ровно три файла комплекта");
  }
  if (fileCount != 3)
    throw std::runtime_error("Каталог модели должен содержать ровно три файла комплекта");
  const std::map<std::string, std::string> actualFiles = {{"encoder.onnx", sha256File(root / "encoder.onnx")},
                                                          {"placement-head.onnx", sha256File(root / "placement-head.onnx")}};
  for (const auto & [name, digest] : actualFiles)
    if (files[name] != digest)
      throw std::runtime_error("Контрольная сумма графа ONNX не совпадает: " + name);
  const std::string digestPayload = "{\"encoder.onnx\":\"" + actualFiles.at("encoder.onnx") + "\",\"placement-head.onnx\":\"" +
                                    actualFiles.at("placement-head.onnx") + "\"}";
  if (metadata["modelSha256"] != sha256Text(digestPayload))
    throw std::runtime_error("Общий SHA-256 комплекта модели не совпадает");
  PolygonModelMetadata result;
  result.modelId = metadata["modelId"].get<std::string>();
  result.modelSha256 = metadata["modelSha256"].get<std::string>();
  result.checkpointSha256 = metadata["checkpointSha256"].get<std::string>();
  result.datasetManifestSha256 = metadata["datasetManifestSha256"].get<std::string>();
  result.hiddenSize = limits["hiddenSize"].get<std::size_t>();
  result.maxInstances = limits["maxInstances"].get<std::size_t>();
  result.maxCandidatesPerPair = limits["maxCandidatesPerPair"].get<std::size_t>();
  if (result.modelId.empty() || result.hiddenSize < 16 || result.hiddenSize % 2 != 0 || result.maxInstances == 0 ||
      result.maxInstances > 100 || result.maxCandidatesPerPair == 0 || result.maxCandidatesPerPair > 250000)
    throw std::runtime_error("Идентичность или размеры полигональной модели некорректны");
  return result;
}

/// Формирует переносимую последовательность SplitMix64.
class SplitMix64
{
public:
  /// Сохраняет начальное 64-битное состояние.
  explicit SplitMix64(std::uint64_t seed)
    : state_(seed)
  {
  }

  /// Возвращает следующее беззнаковое 64-битное значение.
  std::uint64_t next()
  {
    std::uint64_t value = (state_ += SPLITMIX_INCREMENT);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
  }

  /// Возвращает число из полуинтервала [0, 1) с 53 значащими битами.
  double uniform() { return static_cast<double>(next() >> 11) * (1.0 / static_cast<double>(1ULL << 53)); }

private:
  std::uint64_t state_;
};

/// Получает начальное значение независимого нейросетевого прогона.
std::uint64_t rolloutSeed(std::uint64_t seed, std::size_t index)
{
  SplitMix64 generator(seed + static_cast<std::uint64_t>(index));
  return generator.next();
}

/// Выбирает допустимую оценку жадно либо по переносимому распределению.
std::size_t selectLogit(const float * logits, const unsigned char * legal, std::size_t count, SplitMix64 * generator)
{
  std::vector<std::size_t> indices;
  indices.reserve(count);
  for (std::size_t index = 0; index < count; ++index)
    if (!legal || legal[index] != 0)
      indices.push_back(index);
  if (indices.empty())
    throw std::runtime_error("Уровень полигональной модели не содержит допустимых вариантов");
  for (std::size_t index : indices)
    if (!std::isfinite(logits[index]))
      throw std::runtime_error("Полигональная модель вернула нечисловую или бесконечную оценку");
  if (!generator)
    return *std::max_element(indices.begin(), indices.end(),
                             [logits](std::size_t left, std::size_t right) { return logits[left] < logits[right]; });
  const double maximum = logits[*std::max_element(indices.begin(), indices.end(), [logits](std::size_t left, std::size_t right)
                                                  { return logits[left] < logits[right]; })];
  std::vector<double> weights;
  weights.reserve(indices.size());
  for (std::size_t index : indices)
    weights.push_back(std::exp(static_cast<double>(logits[index]) - maximum));
  const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
  const double threshold = generator->uniform() * total;
  double cumulative = 0.0;
  for (std::size_t position = 0; position < indices.size(); ++position)
  {
    cumulative += weights[position];
    if (threshold < cumulative)
      return indices[position];
  }
  return indices.back();
}

/// Проверяет имена входов и выходов графа до первого вычисления.
void validateSessionSignature(const Ort::Session & session, const std::vector<std::string> & inputs,
                              const std::vector<std::string> & outputs)
{
  if (session.GetInputCount() != inputs.size() || session.GetOutputCount() != outputs.size())
    throw std::runtime_error("Число входов или выходов графа ONNX не совпадает с контрактом");
  Ort::AllocatorWithDefaultOptions allocator;
  for (std::size_t index = 0; index < inputs.size(); ++index)
    if (session.GetInputNameAllocated(index, allocator).get() != inputs[index])
      throw std::runtime_error("Имя входа графа ONNX не совпадает: " + inputs[index]);
  for (std::size_t index = 0; index < outputs.size(); ++index)
    if (session.GetOutputNameAllocated(index, allocator).get() != outputs[index])
      throw std::runtime_error("Имя выхода графа ONNX не совпадает: " + outputs[index]);
}

/// Проверяет встроенную в граф идентичность архитектуры, роли и набора операций.
void validateGraphMetadata(const Ort::Session & session, std::string_view graphKind)
{
  Ort::AllocatorWithDefaultOptions allocator;
  const Ort::ModelMetadata metadata = session.GetModelMetadata();
  const auto architecture = metadata.LookupCustomMetadataMapAllocated("aipackaging.architecture", allocator);
  const auto kind = metadata.LookupCustomMetadataMapAllocated("aipackaging.graph", allocator);
  const auto opset = metadata.LookupCustomMetadataMapAllocated("aipackaging.opset", allocator);
  if (!architecture || !kind || !opset || std::string_view(architecture.get()) != "hierarchical-polygon-policy-v1" ||
      std::string_view(kind.get()) != graphKind || std::string_view(opset.get()) != "23")
    throw std::runtime_error("Встроенные свойства графа ONNX не совпадают с контрактом комплекта");
}

/// Возвращает число элементов тензора и проверяет его форму.
std::size_t validateTensor(const Ort::Value & value, const std::vector<std::int64_t> & expected, std::string_view name)
{
  if (!value.IsTensor())
    throw std::runtime_error("Выход ONNX не является тензором: " + std::string(name));
  const auto info = value.GetTensorTypeAndShapeInfo();
  if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetShape() != expected)
    throw std::runtime_error("Форма или тип выхода ONNX не совпадает: " + std::string(name));
  return info.GetElementCount();
}

/// Создаёт тензор ONNX Runtime над владеющим вектором вещественных чисел.
Ort::Value tensor(Ort::MemoryInfo & memory, std::vector<float> & values, const std::vector<std::int64_t> & shape)
{
  return Ort::Value::CreateTensor<float>(memory, values.data(), values.size(), shape.data(), shape.size());
}

/// Сравнивает два действия по всем полям текущего динамического каталога.
std::size_t findAction(const std::vector<PolygonAction> & actions, const PolygonAction & target)
{
  const auto found = std::find(actions.begin(), actions.end(), target);
  if (found == actions.end())
    throw std::runtime_error("Условное действие отсутствует в текущем динамическом каталоге");
  return static_cast<std::size_t>(std::distance(actions.begin(), found));
}

/// Формирует метаданные решения, пригодные для `polygon_solution` v2.
SolverMetadata makeSolverMetadata(const PolygonModelMetadata & model, std::uint64_t seed, std::string mode, std::size_t rollouts)
{
  SolverMetadata metadata;
  metadata.family = SolverFamily::Neural;
  metadata.name = "polygon-onnx-policy-v1";
  metadata.projectVersion = AIPACKAGING_PROJECT_VERSION;
  metadata.revision = AIPACKAGING_BUILD_REVISION;
  metadata.seed = seed;
  metadata.randomIterations = 64;
  metadata.beamWidth = 8;
  metadata.maxExpandedStates = 5000;
  metadata.timeoutMs = 0;
  metadata.modelId = model.modelId;
  metadata.modelSha256 = model.modelSha256;
  metadata.rollouts = rollouts;
  metadata.selectionMode = std::move(mode);
  return metadata;
}
} // namespace

/// Хранит проверенные метаданные, окружение и два последовательных сеанса CPU.
class PolygonOnnxPolicyImpl
{
public:
  /// Создаёт сеансы после проверки файлов и их публичных сигнатур.
  PolygonOnnxPolicyImpl(const std::filesystem::path & root, PolygonModelMetadata metadata)
    : metadata_(std::move(metadata))
    , environment_(ORT_LOGGING_LEVEL_WARNING, "AIPackagingPolygon")
    , options_()
    , encoder_(nullptr)
    , placement_(nullptr)
  {
    options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    options_.SetIntraOpNumThreads(1);
    options_.SetInterOpNumThreads(1);
    options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    encoder_ = Ort::Session(environment_, (root / "encoder.onnx").c_str(), options_);
    placement_ = Ort::Session(environment_, (root / "placement-head.onnx").c_str(), options_);
    validateGraphMetadata(encoder_, "encoder");
    validateGraphMetadata(placement_, "placement-head");
    validateSessionSignature(encoder_, {"sheet", "part_masks", "part_features", "objective"},
                             {"state_embedding", "orientation_embeddings", "instance_logits", "rotation_logits", "value"});
    validateSessionSignature(placement_, {"state_embedding", "orientation_embedding", "raster", "candidate_features"},
                             {"position_logits"});
  }

  /// Выполняет один полный или прерванный эпизод с заданным потоком случайных чисел.
  PolygonPolicyExecutionResult rollout(const PolygonProblem & problem, std::uint64_t seed, bool sampled,
                                       std::size_t reportedRollouts, const PolygonPolicyControl & control,
                                       const std::chrono::steady_clock::time_point & deadline, bool useDeadline) const
  {
    std::string error;
    PolygonLearningConfig learningConfig;
    learningConfig.rewardVersion = 2;
    learningConfig.catalogVersion = PolygonActionCatalogVersion::Legacy;
    std::unique_ptr<PolygonLearningEnvironment> learning = PolygonLearningEnvironment::Create(problem, learningConfig, error);
    if (!learning)
      throw std::runtime_error("Не удалось создать обучаемую среду: " + error);
    const PolygonStaticObservation fixed = learning->staticObservation();
    PolygonDynamicObservation dynamic = learning->resetCompact();
    const std::size_t instances = fixed.instancePartIds.size();
    if (instances == 0 || instances > metadata_.maxInstances)
      throw std::runtime_error("Число экземпляров выходит за ограничения полигональной модели");
    SplitMix64 random(seed);
    SplitMix64 * generator = sampled ? &random : nullptr;
    bool cancelled = false;
    bool timedOut = false;
    while (!learning->isTerminal())
    {
      if (control.cancellationRequested && control.cancellationRequested())
      {
        cancelled = true;
        break;
      }
      if (useDeadline && std::chrono::steady_clock::now() >= deadline)
      {
        timedOut = true;
        break;
      }
      const std::size_t action = selectAction(*learning, fixed, dynamic, generator);
      dynamic = learning->stepCompact(action).observation;
    }
    PolygonSolution solution =
      learning->solution(makeSolverMetadata(metadata_, seed, sampled ? "sampled-best-of" : "greedy", reportedRollouts));
    solution.wireVersion = 2;
    if (timedOut)
      solution.status = SolveStatus::TimedOut;
    const ValidationResult validation = validatePolygonSolution(problem, solution);
    if (!validation.success)
      throw std::runtime_error("Полигональная модель вернула некорректное решение: " + validation.error);
    return {std::move(solution), cancelled, false, {}};
  }

  /// Выбирает три уровня действия по текущим наблюдениям и маскам среды.
  std::size_t selectAction(const PolygonLearningEnvironment & learning, const PolygonStaticObservation & fixed,
                           const PolygonDynamicObservation & dynamic, SplitMix64 * generator) const
  {
    const std::size_t instances = fixed.instancePartIds.size();
    std::vector<float> sheet;
    sheet.reserve(dynamic.occupied.size() + dynamic.clearance.size());
    sheet.insert(sheet.end(), dynamic.occupied.begin(), dynamic.occupied.end());
    sheet.insert(sheet.end(), dynamic.clearance.begin(), dynamic.clearance.end());
    std::vector<float> masks(fixed.partMasks.begin(), fixed.partMasks.end());
    std::vector<float> features = fixed.partFeatures;
    std::vector<float> objective = dynamic.objective;
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<Ort::Value, 4> inputValues = {
      tensor(memory, sheet, {2, 128, 128}), tensor(memory, masks, {static_cast<std::int64_t>(instances), 4, 32, 32}),
      tensor(memory, features, {static_cast<std::int64_t>(instances), 7}), tensor(memory, objective, {7})};
    static constexpr std::array<const char *, 4> INPUT_NAMES = {"sheet", "part_masks", "part_features", "objective"};
    static constexpr std::array<const char *, 5> OUTPUT_NAMES = {"state_embedding", "orientation_embeddings", "instance_logits",
                                                                 "rotation_logits", "value"};
    std::vector<Ort::Value> outputs = encoder_.Run(Ort::RunOptions{nullptr}, INPUT_NAMES.data(), inputValues.data(),
                                                   inputValues.size(), OUTPUT_NAMES.data(), OUTPUT_NAMES.size());
    validateTensor(outputs[0], {static_cast<std::int64_t>(metadata_.hiddenSize)}, "state_embedding");
    validateTensor(outputs[1], {static_cast<std::int64_t>(instances), 4, static_cast<std::int64_t>(metadata_.hiddenSize)},
                   "orientation_embeddings");
    validateTensor(outputs[2], {static_cast<std::int64_t>(instances)}, "instance_logits");
    validateTensor(outputs[3], {static_cast<std::int64_t>(instances), 4}, "rotation_logits");
    validateTensor(outputs[4], {}, "value");
    std::vector<unsigned char> instanceMask(instances, 0);
    for (std::size_t instance = 0; instance < instances; ++instance)
      instanceMask[instance] =
        static_cast<unsigned char>(std::ranges::any_of(dynamic.pairMask.begin() + static_cast<std::ptrdiff_t>(instance * 4),
                                                       dynamic.pairMask.begin() + static_cast<std::ptrdiff_t>(instance * 4 + 4),
                                                       [](unsigned char value) { return value != 0; }));
    const std::size_t instance = selectLogit(outputs[2].GetTensorData<float>(), instanceMask.data(), instances, generator);
    const std::size_t rotation =
      selectLogit(outputs[3].GetTensorData<float>() + instance * 4, dynamic.pairMask.data() + instance * 4, 4, generator);
    const PolygonPlacementObservation placement = learning.placementObservation(instance, static_cast<int>(rotation * 90));
    const std::size_t candidates = placement.actions.size();
    if (candidates == 0 || candidates > metadata_.maxCandidatesPerPair)
      throw std::runtime_error("Число позиций выбранной пары выходит за ограничения модели");
    std::vector<float> state(outputs[0].GetTensorData<float>(), outputs[0].GetTensorData<float>() + metadata_.hiddenSize);
    const float * orientationData = outputs[1].GetTensorData<float>() + (instance * 4 + rotation) * metadata_.hiddenSize;
    std::vector<float> orientation(orientationData, orientationData + metadata_.hiddenSize);
    std::vector<float> raster = placement.channels;
    std::vector<float> candidateFeatures = placement.candidateFeatures;
    std::array<Ort::Value, 4> placementInputs = {tensor(memory, state, {static_cast<std::int64_t>(metadata_.hiddenSize)}),
                                                 tensor(memory, orientation, {static_cast<std::int64_t>(metadata_.hiddenSize)}),
                                                 tensor(memory, raster, {4, 128, 128}),
                                                 tensor(memory, candidateFeatures, {static_cast<std::int64_t>(candidates), 7})};
    static constexpr std::array<const char *, 4> PLACEMENT_NAMES = {"state_embedding", "orientation_embedding", "raster",
                                                                    "candidate_features"};
    static constexpr std::array<const char *, 1> POSITION_NAMES = {"position_logits"};
    std::vector<Ort::Value> positionOutputs =
      placement_.Run(Ort::RunOptions{nullptr}, PLACEMENT_NAMES.data(), placementInputs.data(), placementInputs.size(),
                     POSITION_NAMES.data(), POSITION_NAMES.size());
    validateTensor(positionOutputs[0], {static_cast<std::int64_t>(candidates)}, "position_logits");
    const std::size_t position = selectLogit(positionOutputs[0].GetTensorData<float>(), nullptr, candidates, generator);
    return findAction(learning.actions(), placement.actions[position]);
  }

  PolygonModelMetadata metadata_;
  Ort::Env environment_;
  Ort::SessionOptions options_;
  mutable Ort::Session encoder_;
  mutable Ort::Session placement_;
};

/// Проверяет версию среды выполнения, файлы и переводит исключения в диагностику загрузки.
std::shared_ptr<PolygonOnnxPolicy> PolygonOnnxPolicy::Load(const std::string & directory, std::string & error)
{
  try
  {
    const std::string runtimeVersion = OrtGetApiBase()->GetVersionString();
    if (!runtimeVersion.starts_with("1.29."))
      throw std::runtime_error("Требуется ONNX Runtime версии 1.29.x, обнаружена " + runtimeVersion);
    const std::filesystem::path root(directory);
    PolygonModelMetadata metadata = loadMetadata(root);
    return std::shared_ptr<PolygonOnnxPolicy>(
      new PolygonOnnxPolicy(std::make_unique<PolygonOnnxPolicyImpl>(root, std::move(metadata))));
  }
  catch (const std::exception & exception)
  {
    error = exception.what();
    return nullptr;
  }
}

/// Сохраняет полностью построенную реализацию после успешной проверки загрузки.
PolygonOnnxPolicy::PolygonOnnxPolicy(std::unique_ptr<PolygonOnnxPolicyImpl> impl)
  : impl_(std::move(impl))
{
}

/// Уничтожает сеансы до окружения ONNX Runtime, соблюдая порядок полей реализации.
PolygonOnnxPolicy::~PolygonOnnxPolicy() = default;

/// Возвращает неизменяемые метаданные, уже защищённые SHA-256.
const PolygonModelMetadata & PolygonOnnxPolicy::metadata() const noexcept
{
  return impl_->metadata_;
}

/// Выполняет жадный запуск либо выбирает лучшее из заданного числа прогонов.
PolygonPolicyExecutionResult PolygonOnnxPolicy::run(const PolygonProblem & problem, const PolygonPolicyConfig & config,
                                                    const PolygonPolicyControl & control) const
{
  if (config.rollouts == 0)
    throw std::invalid_argument("Число нейросетевых прогонов должно быть положительным");
  if (config.timeoutMs > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count()))
    throw std::invalid_argument("Ограничение времени нейросетевого запуска слишком велико");
  const std::size_t count = config.mode == PolygonPolicyMode::Greedy ? 1 : config.rollouts;
  const auto started = std::chrono::steady_clock::now();
  const auto deadline = started + std::chrono::milliseconds(config.timeoutMs);
  const bool useDeadline = config.timeoutMs > 0;
  std::optional<PolygonSolution> best;
  bool cancelled = false;
  for (std::size_t index = 0; index < count; ++index)
  {
    const bool sampled = config.mode == PolygonPolicyMode::BestOf;
    const std::uint64_t seed = sampled ? rolloutSeed(config.seed, index) : config.seed;
    PolygonPolicyExecutionResult current = impl_->rollout(problem, seed, sampled, count, control, deadline, useDeadline);
    if (!best || isBetterPolygonSolution(current.solution, *best))
      best = std::move(current.solution);
    cancelled = current.cancelled;
    if (control.progress)
      control.progress(index + 1, count);
    if (cancelled || (useDeadline && std::chrono::steady_clock::now() >= deadline))
      break;
  }
  if (!best)
    throw std::runtime_error("Полигональная модель не сформировала ни одного состояния");
  best->solver = makeSolverMetadata(impl_->metadata_, config.seed,
                                    config.mode == PolygonPolicyMode::Greedy ? "greedy" : "sampled-best-of", count);
  best->metrics.totalTimeUs = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
  const ValidationResult validation = validatePolygonSolution(problem, *best);
  if (!validation.success)
    throw std::runtime_error("Итоговое решение полигональной модели не прошло проверку: " + validation.error);
  return {std::move(*best), cancelled, false, {}};
}

/// Сначала получает гарантированный базовый результат, затем сравнивает его с политикой.
PolygonPolicyExecutionResult PolygonOnnxPolicy::runHybrid(const PolygonProblem & problem,
                                                          const PolygonPolicyConfig & policyConfig,
                                                          const SolverConfig & fallbackConfig,
                                                          const PolygonPolicyControl & control) const
{
  if (policyConfig.timeoutMs > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count()))
    throw std::invalid_argument("Ограничение времени гибридного запуска слишком велико");
  const auto started = std::chrono::steady_clock::now();
  SolverConfig boundedFallback = fallbackConfig;
  if (policyConfig.timeoutMs > 0 && (boundedFallback.timeoutMs == 0 || boundedFallback.timeoutMs > policyConfig.timeoutMs))
    boundedFallback.timeoutMs = policyConfig.timeoutMs;
  PolygonExecutionControl baselineControl;
  baselineControl.cancellationRequested = control.cancellationRequested;
  PolygonSolverExecutionResult baseline =
    runPolygonProblem(problem, boundedFallback, baselineControl, PolygonActionCatalogVersion::Legacy);
  const ValidationResult baselineValidation = validatePolygonSolution(problem, baseline.solution);
  if (!baselineValidation.success)
    throw std::runtime_error("Резервный базовый алгоритм вернул некорректное решение: " + baselineValidation.error);
  if (baseline.cancelled)
    return {std::move(baseline.solution), true, false, {}};
  PolygonPolicyConfig remainingPolicy = policyConfig;
  if (policyConfig.timeoutMs > 0)
  {
    // Гибридный запуск имеет одно ограничение времени: базовый этап не выдаёт
    // нейросетевому этапу новый полный бюджет после своего завершения.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    const auto elapsedMs = static_cast<std::uint64_t>(elapsed.count());
    if (elapsedMs >= policyConfig.timeoutMs)
    {
      if (!baseline.solution.complete())
        baseline.solution.status = SolveStatus::TimedOut;
      return {std::move(baseline.solution), false, false, "Общее ограничение времени исчерпано до нейросетевых прогонов"};
    }
    remainingPolicy.timeoutMs = policyConfig.timeoutMs - elapsedMs;
  }
  try
  {
    PolygonPolicyExecutionResult neural = run(problem, remainingPolicy, control);
    if (neural.cancelled)
      return neural;
    const PolygonSolution & chosen =
      isBetterPolygonSolution(baseline.solution, neural.solution) ? baseline.solution : neural.solution;
    std::string error;
    PolygonLearningConfig learningConfig;
    learningConfig.rewardVersion = 2;
    learningConfig.catalogVersion = PolygonActionCatalogVersion::Legacy;
    std::unique_ptr<PolygonLearningEnvironment> learning = PolygonLearningEnvironment::Create(problem, learningConfig, error);
    if (!learning)
      throw std::runtime_error("Не удалось повторить гибридное решение: " + error);
    learning->resetCompact();
    for (const PolygonPlacement & placement : chosen.placements)
    {
      const auto found = std::find(learning->actions().begin(), learning->actions().end(), placement);
      if (found == learning->actions().end())
        throw std::runtime_error("Гибридное размещение отсутствует в динамическом каталоге");
      learning->stepCompact(static_cast<std::size_t>(std::distance(learning->actions().begin(), found)));
    }
    SolverMetadata metadata = makeSolverMetadata(impl_->metadata_, policyConfig.seed, "hybrid-best-of", policyConfig.rollouts);
    metadata.family = SolverFamily::Hybrid;
    metadata.name = "hybrid-polygon-onnx-policy-v1";
    PolygonSolution result = learning->solution(metadata);
    result.wireVersion = 2;
    const ValidationResult validation = validatePolygonSolution(problem, result);
    if (!validation.success)
      throw std::runtime_error("Гибридное решение не прошло итоговую проверку: " + validation.error);
    return {std::move(result), false, false, {}};
  }
  catch (const std::exception & exception)
  {
    return {std::move(baseline.solution), false, true,
            "Нейросетевая часть завершилась ошибкой; использован резервный алгоритм: " + std::string(exception.what())};
  }
}
} // namespace aipackaging::inference
