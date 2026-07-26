// Project Seoul product runtime: the general planner.

#include "seoul/browser/product/planner.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "seoul/browser/product/plan_json.h"
#include "seoul/browser/tools/tool_registry.h"
#include "seoul/browser/tools/tool_schema.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

SchemaField StringField(const std::string& name, bool required) {
  SchemaField field;
  field.name = name;
  field.kind = SchemaFieldKind::kString;
  field.required = required;
  return field;
}

ToolDescriptor Descriptor(const std::string& id,
                          const std::string& description,
                          RiskCategory risk = RiskCategory::kReadOnly) {
  ToolDescriptor descriptor;
  descriptor.id = ToolId::FromString(id);
  descriptor.name = id;
  descriptor.description = description;
  descriptor.provider = "seoul";
  descriptor.risk = risk;
  descriptor.input_schema.fields.push_back(StringField("query", true));
  descriptor.sensitivity = DataSensitivity::kOrganization;
  return descriptor;
}

ToolPermissionContext AllowAll() {
  ToolPermissionContext context;
  context.max_sensitivity = DataSensitivity::kCredentialAdjacent;
  context.allow_network = true;
  return context;
}

// Held-out vocabulary: these capability descriptions were never special-cased
// anywhere; the planner must select them from their own text.
TEST(PlannerTest, DeterministicSelectionIsDataDriven) {
  ToolRegistry registry;
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.lookup.glossary",
                                       "Looks up terminology definitions in "
                                       "a connected glossary index"))
                  .has_value());
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.observe.telemetry",
                                       "Reads pipeline telemetry queue "
                                       "depth samples"))
                  .has_value());

  Planner planner(registry, ModelPlanRequester());
  const PlannerResult result = planner.BuildDeterministic(
      "look up the definition of the term amortization in the glossary",
      AllowAll());
  ASSERT_TRUE(result.ok) << result.failure;
  ASSERT_EQ(result.plan.steps.size(), 1u);
  EXPECT_EQ(result.plan.steps[0].tool.value(), "info.lookup.glossary");
  // The required string arg was synthesized from the goal, schema-driven.
  const std::string* query = result.plan.steps[0].args.FindString("query");
  ASSERT_TRUE(query);
  EXPECT_FALSE(query->empty());
}

TEST(PlannerTest, NoMatchingCapabilityFailsHonestly) {
  ToolRegistry registry;
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.observe.telemetry",
                                       "Reads pipeline telemetry"))
                  .has_value());
  Planner planner(registry, ModelPlanRequester());
  const PlannerResult result =
      planner.BuildDeterministic("zzqx vvwq ppl", AllowAll());
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.failure.empty());
}

TEST(PlannerTest, UnsatisfiedTopMatchDoesNotBlockRunnableCapability) {
  ToolRegistry registry;

  ToolDescriptor open;
  open.id = ToolId::FromString("browser.tabs.open");
  open.name = "Open tab";
  open.description =
      "Opens a URL in a new tab in the current window and workspace.";
  open.provider = "seoul";
  open.risk = RiskCategory::kReversibleMutation;
  open.sensitivity = DataSensitivity::kOrganization;
  SchemaField url;
  url.name = "url";
  url.kind = SchemaFieldKind::kUrl;
  url.required = true;
  open.input_schema.fields.push_back(std::move(url));
  ASSERT_TRUE(registry.Register(std::move(open)).has_value());

  ToolDescriptor enumerate;
  enumerate.id = ToolId::FromString("browser.tabs.enumerate");
  enumerate.name = "List tabs";
  enumerate.description =
      "Lists open tabs with stable keys, titles, and workspace roles.";
  enumerate.provider = "seoul";
  enumerate.risk = RiskCategory::kReadOnly;
  enumerate.sensitivity = DataSensitivity::kOrganization;
  ASSERT_TRUE(registry.Register(std::move(enumerate)).has_value());

  Planner planner(registry, ModelPlanRequester());
  const PlannerResult result = planner.BuildDeterministic(
      "list the open tabs in this window", AllowAll());
  ASSERT_TRUE(result.ok) << result.failure;
  ASSERT_EQ(result.plan.steps.size(), 1u);
  EXPECT_EQ(result.plan.steps[0].tool.value(), "browser.tabs.enumerate");
  EXPECT_TRUE(result.plan.steps[0].args.empty());
}

TEST(PlannerTest, ApprovalPolicyIsEnforcedOnRiskyCapabilities) {
  ToolRegistry registry;
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.submit.report",
                                       "Submits the prepared report upstream",
                                       RiskCategory::kExternalSideEffect))
                  .has_value());
  Planner planner(registry, ModelPlanRequester());
  const PlannerResult result = planner.BuildDeterministic(
      "submit the prepared report upstream", AllowAll());
  ASSERT_TRUE(result.ok) << result.failure;
  ASSERT_EQ(result.plan.steps.size(), 1u);
  EXPECT_TRUE(result.plan.steps[0].requires_approval);
}

TEST(PlannerTest, FirstUsePolicyMarksExactToolStepForApproval) {
  ToolRegistry registry;
  ToolDescriptor descriptor =
      Descriptor("browser.tabs.open", "Opens one URL");
  descriptor.approval = ApprovalPolicy::kFirstUsePerScope;
  ASSERT_TRUE(registry.Register(std::move(descriptor)).has_value());
  Plan plan;
  PlanStep step;
  step.id = "open";
  step.kind = PlanStepKind::kToolCall;
  step.tool = ToolId::FromString("browser.tabs.open");
  step.args.Set("query", "https://example.test");
  plan.steps.push_back(std::move(step));
  Planner::EnforceApprovalPolicy(plan, registry);
  ASSERT_EQ(plan.steps.size(), 1u);
  EXPECT_TRUE(plan.steps.front().requires_approval);
}

TEST(PlannerTest, ModelOutputIsValidatedAndFallsBackWhenInvalid) {
  base::test::TaskEnvironment environment;
  ToolRegistry registry;
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.lookup.glossary",
                                       "Looks up terminology definitions"))
                  .has_value());

  // A model that plans with a capability that does not exist: the validated
  // path must reject it and fall back to the deterministic plan.
  ModelPlanRequester requester = base::BindRepeating(
      [](const std::string& prompt, bool prefer_local,
         bool allow_cloud_models,
         base::OnceCallback<void(std::optional<base::DictValue>, PlanOrigin)>
             callback) {
        (void)allow_cloud_models;
        base::DictValue plan;
        plan.Set("goal", "x");
        base::ListValue steps;
        base::DictValue step;
        step.Set("id", "step_1");
        step.Set("kind", "tool_call");
        step.Set("tool", "info.invented.capability");
        steps.Append(std::move(step));
        plan.Set("steps", std::move(steps));
        std::move(callback).Run(std::move(plan), PlanOrigin::kLocalModel);
      });

  Planner planner(registry, std::move(requester));
  base::test::TestFuture<PlannerResult> future;
  planner.BuildPlan("look up the glossary definition of gamma", AllowAll(),
                    /*use_model=*/true, /*prefer_local=*/true,
                    future.GetCallback());
  const PlannerResult& result = future.Get();
  ASSERT_TRUE(result.ok) << result.failure;
  EXPECT_EQ(result.origin, PlanOrigin::kDeterministic);
  EXPECT_EQ(result.plan.steps[0].tool.value(), "info.lookup.glossary");
}

TEST(PlannerTest, PropagatesCloudPolicyToModelRequester) {
  base::test::TaskEnvironment environment;
  ToolRegistry registry;
  bool observed_allow_cloud = true;
  ModelPlanRequester requester = base::BindRepeating(
      [](bool* observed_allow_cloud, const std::string& prompt,
         bool prefer_local, bool allow_cloud_models,
         base::OnceCallback<void(std::optional<base::DictValue>, PlanOrigin)>
             callback) {
        (void)prompt;
        (void)prefer_local;
        *observed_allow_cloud = allow_cloud_models;
        std::move(callback).Run(std::nullopt, PlanOrigin::kDeterministic);
      },
      &observed_allow_cloud);
  Planner planner(registry, std::move(requester));
  base::test::TestFuture<PlannerResult> future;
  planner.BuildPlan("private local request", AllowAll(),
                    /*use_model=*/true, /*prefer_local=*/true,
                    /*allow_cloud_models=*/false, future.GetCallback());
  (void)future.Get();
  EXPECT_FALSE(observed_allow_cloud);
}

TEST(PlannerTest, PromptCarriesCapabilityContracts) {
  ToolRegistry registry;
  ASSERT_TRUE(registry
                  .Register(Descriptor("info.lookup.glossary",
                                       "Looks up terminology definitions"))
                  .has_value());
  const std::string prompt = Planner::BuildPlanningPrompt(
      "define amortization", registry.ListAvailable(AllowAll()));
  EXPECT_NE(prompt.find("info.lookup.glossary"), std::string::npos);
  EXPECT_NE(prompt.find("input_schema"), std::string::npos);
  EXPECT_NE(prompt.find("Do not emit code"), std::string::npos);
}

TEST(PlanJsonTest, RoundTripsAndRejectsMalformed) {
  Plan plan;
  plan.goal = "test goal";
  PlanStep step;
  step.id = "step_1";
  step.kind = PlanStepKind::kToolCall;
  step.tool = ToolId::FromString("info.lookup.glossary");
  step.args.Set("query", "gamma");
  step.requires_approval = true;
  plan.steps.push_back(std::move(step));
  PlanStep gate;
  gate.id = "gate_1";
  gate.kind = PlanStepKind::kApprovalGate;
  gate.prompt = "Continue?";
  StepGuard guard;
  guard.depends_on_step = "step_1";
  gate.guard = guard;
  plan.steps.push_back(std::move(gate));

  const std::optional<Plan> round = PlanFromValue(PlanToValue(plan));
  ASSERT_TRUE(round.has_value());
  ASSERT_EQ(round->steps.size(), 2u);
  EXPECT_EQ(round->steps[0].tool.value(), "info.lookup.glossary");
  EXPECT_TRUE(round->steps[0].requires_approval);
  ASSERT_TRUE(round->steps[1].guard.has_value());
  EXPECT_EQ(round->steps[1].guard->depends_on_step, "step_1");

  // Unknown step kinds and invalid tool ids are rejected, not skipped.
  base::DictValue bad;
  bad.Set("goal", "g");
  base::ListValue steps;
  base::DictValue bad_step;
  bad_step.Set("id", "s");
  bad_step.Set("kind", "shell_command");
  steps.Append(std::move(bad_step));
  bad.Set("steps", std::move(steps));
  EXPECT_FALSE(PlanFromValue(bad).has_value());
}

}  // namespace
}  // namespace seoul
