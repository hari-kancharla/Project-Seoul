// Project Seoul Live Collections runtime tests.

#include "seoul/browser/product/live_collection_coordinator.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "seoul/browser/semantic/semantic_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

FieldSpec Field(const char* id,
                FieldPrimitive primitive,
                SemanticRole role,
                bool nullable = false) {
  FieldSpec field;
  field.id = id;
  field.label = id;
  field.primitive = primitive;
  field.role = role;
  field.nullable = nullable;
  return field;
}

class ManualCollectionExecutor : public CapabilityExecutor {
 public:
  explicit ManualCollectionExecutor(ToolId id) : id_(std::move(id)) {}
  ~ManualCollectionExecutor() override = default;

  ToolId capability_id() const override { return id_; }

  void Execute(CapabilityRequest request,
               CapabilityCallback callback) override {
    ++execute_count;
    last_args = request.args.Clone();
    pending = std::move(callback);
  }

  void Cancel(const TaskId& task_id, const std::string& step_id) override {
    ++cancel_count;
    pending.Reset();
  }

  void Complete(CapabilityOutcome outcome) {
    ASSERT_TRUE(pending);
    std::move(pending).Run(std::move(outcome));
  }

  ToolId id_;
  int execute_count = 0;
  int cancel_count = 0;
  base::DictValue last_args;
  CapabilityCallback pending;
};

ToolDescriptor SourceDescriptor(const char* id,
                                const char* name = "Test source") {
  ToolDescriptor descriptor;
  descriptor.id = ToolId::FromString(id);
  descriptor.name = name;
  descriptor.description = "Returns a verified collection for testing.";
  descriptor.provider = "seoul";
  descriptor.risk = RiskCategory::kReadOnly;
  descriptor.approval = ApprovalPolicy::kNeverRequired;
  descriptor.idempotency = IdempotencyClass::kIdempotent;
  descriptor.timeout = base::Seconds(1);
  return descriptor;
}

SchemaField RequiredString(const char* name) {
  SchemaField field;
  field.name = name;
  field.kind = SchemaFieldKind::kString;
  field.required = true;
  field.description = "Source query";
  return field;
}

CapabilityOutcome SuccessfulOutcome(base::Time now) {
  SemanticResult result;
  result.schema.shape = SemanticShape::kEntityCollection;
  result.schema.fields = {
      Field("id", FieldPrimitive::kString, SemanticRole::kIdentifier),
      Field("title", FieldPrimitive::kString, SemanticRole::kName),
      Field("detail", FieldPrimitive::kString,
            SemanticRole::kDescription, true),
      Field("url", FieldPrimitive::kString, SemanticRole::kUrl, true),
      Field("state", FieldPrimitive::kString, SemanticRole::kStatus, true),
  };
  base::ListValue rows;
  base::DictValue first;
  first.Set("id", "item-1");
  first.Set("title", "Verified item");
  first.Set("detail", "Real semantic output");
  first.Set("url", "https://example.test/item-1");
  first.Set("state", "ready");
  rows.Append(std::move(first));
  result.data = base::Value(std::move(rows));
  result.provenance.base.source_name = "Test source";
  result.provenance.base.retrieved_at = now;
  result.provenance.base.effective_at = now;
  result.provenance.provider = "seoul";

  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary = "Read the source.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "test_observation";
  outcome.semantic = std::move(result);
  return outcome;
}

class LiveCollectionCoordinatorTest : public testing::Test {
 protected:
  LiveCollectionCoordinatorTest()
      : library_(base::BindRepeating(
            &LiveCollectionCoordinatorTest::Now, base::Unretained(this))) {}

  void TearDown() override {
    if (coordinator_) {
      coordinator_->Shutdown();
    }
  }

  base::Time Now() const { return task_environment_.GetMockClock()->Now(); }

  ManualCollectionExecutor* Register(ToolDescriptor descriptor) {
    const ToolId id = descriptor.id;
    EXPECT_TRUE(capabilities_.Register(std::move(descriptor)).has_value());
    auto executor = std::make_unique<ManualCollectionExecutor>(id);
    ManualCollectionExecutor* raw = executor.get();
    EXPECT_TRUE(executors_.Register(std::move(executor)));
    return raw;
  }

  void CreateCoordinator() {
    coordinator_ = std::make_unique<LiveCollectionCoordinator>(
        &library_, &capabilities_, &executors_);
  }

  LiveCollectionId CreateCollection(const ToolId& source,
                                    const std::string& locator = "") {
    LiveCollectionDefinition definition;
    definition.name = "My live collection";
    definition.refresh_capability = source;
    definition.source_locator = locator;
    definition.scope_window = "w-7";
    definition.refresh_interval_minutes = 15;
    LiveCollectionUpsertResult created =
        coordinator_->Upsert(std::move(definition), context_);
    EXPECT_TRUE(created.has_value());
    return created.has_value() ? *created : LiveCollectionId();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  LibraryService library_;
  ToolRegistry capabilities_;
  CapabilityExecutorRegistry executors_;
  ToolPermissionContext context_;
  std::unique_ptr<LiveCollectionCoordinator> coordinator_;
};

TEST_F(LiveCollectionCoordinatorTest,
       EligibilityIsReadOnlyApprovalFreeAndSchemaDriven) {
  ToolDescriptor eligible = SourceDescriptor("info.test.eligible", "Eligible");
  eligible.input_schema.fields.push_back(RequiredString("query"));
  Register(std::move(eligible));

  ToolDescriptor mutating = SourceDescriptor("info.test.mutating");
  mutating.risk = RiskCategory::kReversibleMutation;
  Register(std::move(mutating));

  ToolDescriptor approval = SourceDescriptor("info.test.approval");
  approval.approval = ApprovalPolicy::kFirstUsePerScope;
  Register(std::move(approval));

  ToolDescriptor complex = SourceDescriptor("info.test.complex");
  complex.input_schema.fields.push_back(RequiredString("first"));
  complex.input_schema.fields.push_back(RequiredString("second"));
  Register(std::move(complex));

  CreateCoordinator();
  const std::vector<LiveCollectionSource> sources =
      coordinator_->EligibleSources(context_);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0].capability.value(), "info.test.eligible");
  EXPECT_TRUE(sources[0].source_required);
  EXPECT_EQ(sources[0].source_field, "query");
}

TEST_F(LiveCollectionCoordinatorTest,
       UpsertRejectsUnsafeUnavailableAndMalformedSources) {
  ToolDescriptor mutating = SourceDescriptor("info.test.mutating");
  mutating.risk = RiskCategory::kExternalSideEffect;
  Register(std::move(mutating));

  ToolDescriptor complex = SourceDescriptor("info.test.complex");
  complex.input_schema.fields.push_back(RequiredString("first"));
  complex.input_schema.fields.push_back(RequiredString("second"));
  Register(std::move(complex));

  ToolDescriptor unavailable = SourceDescriptor("info.test.unavailable");
  const ToolId unavailable_id = unavailable.id;
  Register(std::move(unavailable));
  ASSERT_TRUE(capabilities_.SetAvailability(
      unavailable_id, AvailabilityState::kUnavailable, "offline"));
  CreateCoordinator();

  auto attempt = [&](const char* capability) {
    LiveCollectionDefinition definition;
    definition.name = "Rejected";
    definition.refresh_capability = ToolId::FromString(capability);
    definition.scope_window = "w-7";
    return coordinator_->Upsert(std::move(definition), context_);
  };
  EXPECT_EQ(attempt("info.test.mutating").error(),
            LiveCollectionRuntimeError::kSourceNotBackgroundSafe);
  EXPECT_EQ(attempt("info.test.complex").error(),
            LiveCollectionRuntimeError::kSourceSchemaUnsupported);
  EXPECT_EQ(attempt("info.test.unavailable").error(),
            LiveCollectionRuntimeError::kSourceUnavailable);
  EXPECT_EQ(library_.live_collection_count(), 0u);
}

TEST_F(LiveCollectionCoordinatorTest,
       RefreshMapsVerifiedSemanticRolesAndTypedSourceInput) {
  ToolDescriptor descriptor = SourceDescriptor("info.test.feed");
  descriptor.input_schema.fields.push_back(RequiredString("query"));
  ManualCollectionExecutor* executor = Register(std::move(descriptor));
  CreateCoordinator();
  const LiveCollectionId id =
      CreateCollection(ToolId::FromString("info.test.feed"), "release notes");

  std::optional<LiveCollectionRuntimeStatus> completion;
  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](std::optional<LiveCollectionRuntimeStatus>*
                                       destination,
                                   LiveCollectionRuntimeStatus result) {
                                  destination->emplace(std::move(result));
                                },
                                &completion))
                  .has_value());
  EXPECT_EQ(library_.FindLiveCollection(id)->refresh_state,
            LiveRefreshState::kRefreshing);
  const std::string* query = executor->last_args.FindString("query");
  ASSERT_TRUE(query);
  EXPECT_EQ(*query, "release notes");

  executor->Complete(SuccessfulOutcome(Now()));
  ASSERT_TRUE(completion.has_value());
  EXPECT_TRUE(completion->has_value());
  const LiveCollectionRecord* record = library_.FindLiveCollection(id);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->refresh_state, LiveRefreshState::kReady);
  ASSERT_EQ(record->items.size(), 1u);
  EXPECT_EQ(record->items[0].stable_key, "item-1");
  EXPECT_EQ(record->items[0].title, "Verified item");
  EXPECT_EQ(record->items[0].subtitle, "Real semantic output");
  EXPECT_EQ(record->items[0].url, "https://example.test/item-1");
  EXPECT_TRUE(record->items[0].actionable);
  EXPECT_FALSE(record->last_success_at.is_null());
}

TEST_F(LiveCollectionCoordinatorTest,
       InvalidProviderResultPreservesLastSuccessfulItems) {
  ManualCollectionExecutor* executor =
      Register(SourceDescriptor("info.test.feed"));
  CreateCoordinator();
  const LiveCollectionId id =
      CreateCollection(ToolId::FromString("info.test.feed"));

  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](LiveCollectionRuntimeStatus) {}))
                  .has_value());
  executor->Complete(SuccessfulOutcome(Now()));
  ASSERT_EQ(library_.FindLiveCollection(id)->items.size(), 1u);

  std::optional<LiveCollectionRuntimeStatus> completion;
  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](std::optional<LiveCollectionRuntimeStatus>*
                                       destination,
                                   LiveCollectionRuntimeStatus result) {
                                  destination->emplace(std::move(result));
                                },
                                &completion))
                  .has_value());
  CapabilityOutcome invalid = SuccessfulOutcome(Now());
  invalid.semantic->schema.fields.erase(
      invalid.semantic->schema.fields.begin());
  executor->Complete(std::move(invalid));

  ASSERT_TRUE(completion.has_value());
  ASSERT_FALSE(completion->has_value());
  EXPECT_EQ(completion->error(),
            LiveCollectionRuntimeError::kInvalidResult);
  const LiveCollectionRecord* record = library_.FindLiveCollection(id);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->refresh_state, LiveRefreshState::kError);
  ASSERT_EQ(record->items.size(), 1u);
  EXPECT_EQ(record->items[0].stable_key, "item-1");
  EXPECT_FALSE(record->last_error.empty());
}

TEST_F(LiveCollectionCoordinatorTest,
       DisablingCollectionCancelsInFlightRefreshWithoutLateMutation) {
  ManualCollectionExecutor* executor =
      Register(SourceDescriptor("info.test.feed"));
  CreateCoordinator();
  const LiveCollectionId id =
      CreateCollection(ToolId::FromString("info.test.feed"));

  std::optional<LiveCollectionRuntimeStatus> completion;
  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](std::optional<LiveCollectionRuntimeStatus>*
                                       destination,
                                   LiveCollectionRuntimeStatus result) {
                                  destination->emplace(std::move(result));
                                },
                                &completion))
                  .has_value());
  ASSERT_TRUE(coordinator_
                  ->SetEnabled(id, false, "", context_)
                  .has_value());
  EXPECT_EQ(executor->cancel_count, 1);
  ASSERT_TRUE(completion.has_value());
  EXPECT_EQ(completion->error(), LiveCollectionRuntimeError::kCancelled);
  const LiveCollectionRecord* record = library_.FindLiveCollection(id);
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->definition.enabled);
  EXPECT_EQ(record->refresh_state, LiveRefreshState::kIdle);
  EXPECT_FALSE(coordinator_->IsRefreshing(id));
}

TEST_F(LiveCollectionCoordinatorTest,
       RejectedReconfigurationLeavesInFlightRefreshIntact) {
  ManualCollectionExecutor* executor =
      Register(SourceDescriptor("info.test.feed"));
  CreateCoordinator();
  const LiveCollectionId id =
      CreateCollection(ToolId::FromString("info.test.feed"));

  std::optional<LiveCollectionRuntimeStatus> completion;
  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](std::optional<LiveCollectionRuntimeStatus>*
                                       destination,
                                   LiveCollectionRuntimeStatus result) {
                                  destination->emplace(std::move(result));
                                },
                                &completion))
                  .has_value());
  LiveCollectionDefinition invalid =
      library_.FindLiveCollection(id)->definition;
  invalid.name.clear();
  invalid.source_locator = "changed source";
  const LiveCollectionUpsertResult rejected =
      coordinator_->Upsert(std::move(invalid), context_);

  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(),
            LiveCollectionRuntimeError::kInvalidDefinition);
  EXPECT_EQ(executor->cancel_count, 0);
  EXPECT_TRUE(coordinator_->IsRefreshing(id));
  EXPECT_EQ(library_.FindLiveCollection(id)->refresh_state,
            LiveRefreshState::kRefreshing);
  EXPECT_FALSE(completion.has_value());

  executor->Complete(SuccessfulOutcome(Now()));
  ASSERT_TRUE(completion.has_value());
  EXPECT_TRUE(completion->has_value());
  EXPECT_EQ(library_.FindLiveCollection(id)->refresh_state,
            LiveRefreshState::kReady);
}

TEST_F(LiveCollectionCoordinatorTest,
       TimeoutCancelsExecutorAndRecordsTruthfulError) {
  ManualCollectionExecutor* executor =
      Register(SourceDescriptor("info.test.feed"));
  CreateCoordinator();
  const LiveCollectionId id =
      CreateCollection(ToolId::FromString("info.test.feed"));

  std::optional<LiveCollectionRuntimeStatus> completion;
  ASSERT_TRUE(coordinator_
                  ->Refresh(id, LiveWindowKey::FromSessionId(7), context_,
                            base::BindOnce(
                                [](std::optional<LiveCollectionRuntimeStatus>*
                                       destination,
                                   LiveCollectionRuntimeStatus result) {
                                  destination->emplace(std::move(result));
                                },
                                &completion))
                  .has_value());
  task_environment_.FastForwardBy(base::Seconds(1));
  ASSERT_TRUE(completion.has_value());
  EXPECT_EQ(completion->error(),
            LiveCollectionRuntimeError::kRefreshTimedOut);
  EXPECT_EQ(executor->cancel_count, 1);
  const LiveCollectionRecord* record = library_.FindLiveCollection(id);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->refresh_state, LiveRefreshState::kError);
  EXPECT_EQ(record->last_error, "The collection source timed out.");
}

}  // namespace
}  // namespace seoul
