// Project Seoul semantic data fabric.
// Conformance tests for the canonical semantic wire codec. These consume the
// SAME fixture corpus (protocol/fixtures/, mirrored to src/seoul/protocol/ by
// materialize.sh) as the TypeScript protocol tests, so the two languages
// cannot drift apart without a test failing.

#include "seoul/browser/semantic/semantic_wire.h"

#include <string>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "seoul/browser/semantic/semantic_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

base::FilePath FixtureDir() {
  base::FilePath src_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_root));
  return src_root.AppendASCII("seoul/protocol/fixtures");
}

base::Value ReadFixture(const std::string& relative) {
  std::string contents;
  const base::FilePath path = FixtureDir().AppendASCII(relative);
  CHECK(base::ReadFileToString(path, &contents))
      << "missing fixture " << path;
  std::optional<base::Value> parsed = base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  CHECK(parsed.has_value()) << "unparseable fixture " << relative;
  return std::move(parsed.value());
}

// Every semantic fixture in the shared corpus. Kept in sync with
// protocol/fixtures/semantic/ by SemanticCorpusIsFullyCovered below.
constexpr const char* kSemanticCases[] = {
    "scalar",          "record",          "collection",
    "table",           "time-series",     "intervals",
    "hierarchy",       "graph",           "geospatial",
    "document",        "citations",       "form",
    "action-set",      "diff",            "code",
    "composite",       "partial-result",  "streaming-update",
    "source-conflict", "error-result",
};

TEST(SemanticWireConformanceTest, EveryFixtureParsesValidatesAndRoundTrips) {
  for (const char* name : kSemanticCases) {
    SCOPED_TRACE(name);
    base::Value doc = ReadFixture(std::string("semantic/") + name + ".json");

    auto parsed = ParseSemanticResult(doc);
    ASSERT_TRUE(parsed.has_value())
        << "parse failed: "
        << SemanticFabricErrorToString(parsed.error().error) << " ("
        << parsed.error().detail << ")";

    auto valid = ValidateSemanticResult(parsed.value());
    ASSERT_TRUE(valid.has_value())
        << "validation failed: "
        << SemanticFabricErrorToString(valid.error().error) << " ("
        << valid.error().detail << ")";

    auto reparsed =
        ParseSemanticResult(base::Value(SemanticResultToValue(parsed.value())));
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(parsed.value(), reparsed.value()) << "round trip changed result";
  }
}

TEST(SemanticWireConformanceTest, SchemaOnlyCodecUsesCanonicalValidation) {
  const base::Value fixture = ReadFixture("semantic/collection.json");
  const base::DictValue* schema_value = fixture.GetDict().FindDict("schema");
  ASSERT_TRUE(schema_value);
  auto schema = ParseSemanticSchema(base::Value(schema_value->Clone()));
  ASSERT_TRUE(schema.has_value());
  ASSERT_TRUE(ValidateSemanticSchema(schema.value()).has_value());
  auto round_trip = ParseSemanticSchema(
      base::Value(SemanticSchemaToValue(schema.value())));
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip.value(), schema.value());
}

TEST(SemanticWireConformanceTest, SemanticCorpusIsFullyCovered) {
  base::FileEnumerator files(FixtureDir().AppendASCII("semantic"),
                             /*recursive=*/false,
                             base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.json"));
  size_t result_documents = 0;
  for (base::FilePath path = files.Next(); !path.empty();
       path = files.Next()) {
    // The streaming append document is a rows-only companion, not a result.
    if (path.BaseName().value().find("append") != std::string::npos) {
      continue;
    }
    result_documents++;
  }
  EXPECT_EQ(result_documents, std::size(kSemanticCases))
      << "fixture corpus and kSemanticCases have diverged";
}

TEST(SemanticWireConformanceTest, StreamingAppendMergesAtomically) {
  auto base_result =
      ParseSemanticResult(ReadFixture("semantic/streaming-update.json"));
  ASSERT_TRUE(base_result.has_value());
  ASSERT_EQ(base_result->state, ResultState::kStreaming);
  ASSERT_EQ(base_result->data.GetList().size(), 3u);

  base::Value append = ReadFixture("semantic/streaming-update.append.json");
  const base::ListValue* rows = append.GetDict().FindList("rows");
  ASSERT_TRUE(rows);

  SemanticResult merged = std::move(base_result.value());
  auto result = MergeStreamingRows(merged, *rows);
  ASSERT_TRUE(result.has_value())
      << SemanticFabricErrorToString(result.error().error);
  EXPECT_EQ(merged.data.GetList().size(), 6u);
}

TEST(SemanticWireConformanceTest, FutureProtocolVersionIsRejected) {
  auto parsed = ParseSemanticResult(
      ReadFixture("compat/semantic-future-version.json"));
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().error,
            SemanticFabricError::kUnsupportedSchemaVersion);
}

TEST(SemanticWireConformanceTest, UnknownKeysAndRolesAreRejected) {
  auto extra_key =
      ParseSemanticResult(ReadFixture("invalid/semantic-extra-key.json"));
  ASSERT_FALSE(extra_key.has_value());
  EXPECT_EQ(extra_key.error().error, SemanticFabricError::kUnknownField);

  auto bad_role =
      ParseSemanticResult(ReadFixture("invalid/semantic-unknown-role.json"));
  EXPECT_FALSE(bad_role.has_value());
}

TEST(SemanticWireConformanceTest, EnumWireNamesRoundTrip) {
  for (auto primitive :
       {FieldPrimitive::kString, FieldPrimitive::kInteger,
        FieldPrimitive::kNumber, FieldPrimitive::kBoolean,
        FieldPrimitive::kTimestamp}) {
    FieldPrimitive out;
    EXPECT_TRUE(FieldPrimitiveFromWire(FieldPrimitiveToWire(primitive), &out));
    EXPECT_EQ(out, primitive);
  }
  for (auto value_class : {ValueClass::kCategorical, ValueClass::kOrdinal,
                           ValueClass::kContinuous, ValueClass::kFreeText}) {
    ValueClass out;
    EXPECT_TRUE(ValueClassFromWire(ValueClassToWire(value_class), &out));
    EXPECT_EQ(out, value_class);
  }
  for (auto sensitivity :
       {FieldSensitivity::kPublic, FieldSensitivity::kPersonal,
        FieldSensitivity::kSensitive}) {
    FieldSensitivity out;
    EXPECT_TRUE(
        FieldSensitivityFromWire(FieldSensitivityToWire(sensitivity), &out));
    EXPECT_EQ(out, sensitivity);
  }
  for (auto state : {ResultState::kComplete, ResultState::kPartial,
                     ResultState::kStreaming, ResultState::kFailed}) {
    ResultState out;
    EXPECT_TRUE(ResultStateFromWire(ResultStateToWire(state), &out));
    EXPECT_EQ(out, state);
  }
}

}  // namespace
}  // namespace seoul
