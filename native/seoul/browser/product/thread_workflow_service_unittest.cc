// Project Seoul product runtime: thread and workflow services.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "seoul/browser/product/task_service.h"
#include "seoul/browser/product/thread_service.h"
#include "seoul/browser/product/workflow_service.h"
#include "seoul/browser/tools/tool_schema.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

base::RepeatingCallback<base::Time()> FixedClock() {
  return base::BindRepeating(
      [] { return base::Time::UnixEpoch() + base::Days(20000); });
}

class SuccessfulWorkflowExecutor : public CapabilityExecutor {
public:
  ToolId capability_id() const override {
    return ToolId::FromString("info.read.inventory");
  }

  void Execute(CapabilityRequest request,
               CapabilityCallback callback) override {
    CapabilityOutcome outcome;
    outcome.step.status = StepStatus::kSucceeded;
    outcome.step.observed_summary = "inventory read";
    outcome.step.verification.verified = true;
    outcome.step.verification.method = "postcondition";
    std::move(callback).Run(std::move(outcome));
  }
};

TEST(ThreadServiceTest, CrudAndAttachDetach) {
  ThreadService service(FixedClock());
  const std::string id = service.CreateThread("Research");
  ASSERT_FALSE(id.empty());
  EXPECT_TRUE(service.RenameThread(id, "Deep research"));

  ContextItem excerpt;
  excerpt.kind = ContextItemKind::kExcerpt;
  excerpt.title = "Passage";
  excerpt.text = "a user-selected passage";
  excerpt.reference = "t-42";
  const ContextResult<std::string> item_id =
      service.AttachItem(id, std::move(excerpt));
  ASSERT_TRUE(item_id.has_value());
  EXPECT_EQ(service.FindThread(id)->items().size(), 1u);
  EXPECT_TRUE(service.DetachItem(id, item_id.value()));
  EXPECT_TRUE(service.ArchiveThread(id));
  EXPECT_TRUE(service.FindThread(id)->archived());
  EXPECT_TRUE(service.ReopenThread(id));
  EXPECT_TRUE(service.DeleteThread(id));
  EXPECT_FALSE(service.FindThread(id));
}

TEST(ThreadServiceTest, SensitiveItemsAreRejected) {
  ThreadService service(FixedClock());
  const std::string id = service.CreateThread("Research");
  ContextItem sensitive;
  sensitive.kind = ContextItemKind::kExcerpt;
  sensitive.title = "Password field";
  sensitive.text = "hunter2";
  sensitive.reference = "t-1";
  sensitive.flagged_sensitive = true;
  const ContextResult<std::string> result =
      service.AttachItem(id, std::move(sensitive));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ContextError::kSensitiveItemRejected);
}

TEST(ThreadServiceTest, PersistsAndRestores) {
  ThreadService service(FixedClock());
  const std::string id = service.CreateThread("Research");
  ContextItem note;
  note.kind = ContextItemKind::kNote;
  note.title = "Note";
  note.text = "remember this";
  ASSERT_TRUE(service.AttachItem(id, std::move(note)).has_value());
  service.ArchiveThread(id);

  ThreadService restored(FixedClock());
  restored.RestorePersistedState(service.TakePersistedState());
  const ContextThread *thread = restored.FindThread(id);
  ASSERT_TRUE(thread);
  EXPECT_TRUE(thread->archived());
  EXPECT_EQ(thread->items().size(), 1u);
}

TEST(ThreadServiceTest, ProjectScopeAndChangeNotificationsPersist) {
  int changes = 0;
  ThreadService service(
      FixedClock(),
      base::BindRepeating([](int* changes) { ++*changes; }, &changes));
  const std::string id =
      service.CreateThread("Project chat", "workspace-project");
  ASSERT_FALSE(id.empty());
  ASSERT_EQ(changes, 1);
  const std::vector<ThreadSummary> summaries = service.Summaries();
  ASSERT_EQ(summaries.size(), 1u);
  EXPECT_EQ(summaries.front().workspace_id, "workspace-project");

  ContextItem note;
  note.kind = ContextItemKind::kNote;
  note.title = "You";
  note.text = "Keep this with the project.";
  ASSERT_TRUE(service.AttachItem(id, std::move(note)).has_value());
  EXPECT_EQ(changes, 2);

  ThreadService restored(FixedClock());
  restored.RestorePersistedState(service.TakePersistedState());
  const std::vector<ThreadSummary> restored_summaries = restored.Summaries();
  ASSERT_EQ(restored_summaries.size(), 1u);
  EXPECT_EQ(restored_summaries.front().workspace_id, "workspace-project");
  EXPECT_EQ(restored_summaries.front().item_count, 1u);
}

class WorkflowServiceTest : public testing::Test {
protected:
  WorkflowServiceTest()
      : planner_(registry_, ModelPlanRequester()),
        tasks_(&registry_, &executors_, &planner_, FixedClock()),
        workflows_(&tasks_, FixedClock()) {
    ToolDescriptor descriptor;
    descriptor.id = ToolId::FromString("info.read.inventory");
    descriptor.name = "inventory reader";
    descriptor.description = "reads the fixture inventory records";
    descriptor.provider = "seoul";
    SchemaField query;
    query.name = "query";
    query.kind = SchemaFieldKind::kString;
    query.required = true;
    descriptor.input_schema.fields.push_back(std::move(query));
    descriptor.sensitivity = DataSensitivity::kOrganization;
    CHECK(registry_.Register(std::move(descriptor)).has_value());
    CHECK(executors_.Register(std::make_unique<SuccessfulWorkflowExecutor>()));
  }

  base::test::TaskEnvironment environment_;
  ToolRegistry registry_;
  CapabilityExecutorRegistry executors_;
  Planner planner_;
  TaskService tasks_;
  WorkflowService workflows_;
};

TEST_F(WorkflowServiceTest, SaveEditExportImportRoundTrip) {
  WorkflowDefinition definition;
  definition.name = "Read inventory";
  WorkflowNode node;
  node.id = "read_1";
  node.kind = WorkflowNodeKind::kToolStep;
  node.label = "Read";
  node.tool = ToolId::FromString("info.read.inventory");
  node.args.Set("query", "all");
  definition.nodes.push_back(std::move(node));

  const WorkflowId id = workflows_.SaveWorkflow(std::move(definition));
  ASSERT_TRUE(id.is_valid());

  WorkflowNode second;
  second.id = "read_2";
  second.kind = WorkflowNodeKind::kToolStep;
  second.label = "Read again";
  second.tool = ToolId::FromString("info.read.inventory");
  second.args.Set("query", "all");
  ASSERT_TRUE(
      workflows_.AddNode(id, std::move(second), std::string()).has_value());
  const WorkflowDefinition *edited = workflows_.Find(id);
  ASSERT_TRUE(edited);
  ASSERT_EQ(edited->edges.size(), 1u);
  EXPECT_EQ(edited->edges[0].from, "read_1");
  EXPECT_EQ(edited->edges[0].to, "read_2");

  const std::optional<base::DictValue> exported = workflows_.Export(id);
  ASSERT_TRUE(exported.has_value());
  const std::optional<WorkflowId> imported =
      workflows_.Import(base::Value(exported->Clone()));
  ASSERT_TRUE(imported.has_value());
  EXPECT_NE(imported->value(), id.value());
  EXPECT_EQ(workflows_.Find(imported.value())->nodes.size(), 2u);
}

TEST_F(WorkflowServiceTest, SaveTaskAsWorkflowUsesTypedPlanOnly) {
  Plan plan;
  plan.goal = "read the inventory";
  PlanStep step;
  step.id = "step_1";
  step.kind = PlanStepKind::kToolCall;
  step.tool = ToolId::FromString("info.read.inventory");
  step.args.Set("query", "all");
  plan.steps.push_back(std::move(step));

  ToolPermissionContext context;
  context.max_sensitivity = DataSensitivity::kCredentialAdjacent;
  const TaskId task_id = tasks_.StartTaskWithPlan(
      "read the inventory", std::move(plan), PlanOrigin::kDeterministic,
      LiveWindowKey::FromSessionId(7), context);
  ASSERT_TRUE(task_id.is_valid());

  const std::optional<WorkflowId> workflow_id =
      workflows_.SaveTaskAsWorkflow(task_id, "Inventory check");
  ASSERT_TRUE(workflow_id.has_value());
  const WorkflowDefinition *definition = workflows_.Find(workflow_id.value());
  ASSERT_TRUE(definition);
  ASSERT_EQ(definition->nodes.size(), 1u);
  // The node is a typed capability call: no coordinates, no indices.
  EXPECT_EQ(definition->nodes[0].tool.value(), "info.read.inventory");
  EXPECT_EQ(*definition->nodes[0].args.FindString("query"), "all");
}

TEST_F(WorkflowServiceTest, RejectsSaveFromIncompleteTask) {
  Plan plan;
  plan.goal = "wait for approval";
  PlanStep step;
  step.id = "approval";
  step.kind = PlanStepKind::kApprovalGate;
  step.prompt = "Continue?";
  plan.steps.push_back(std::move(step));

  ToolPermissionContext context;
  context.max_sensitivity = DataSensitivity::kCredentialAdjacent;
  const TaskId task_id = tasks_.StartTaskWithPlan(
      "wait for approval", std::move(plan), PlanOrigin::kDeterministic,
      LiveWindowKey::FromSessionId(7), context);
  ASSERT_TRUE(task_id.is_valid());
  EXPECT_FALSE(
      workflows_.SaveTaskAsWorkflow(task_id, "Incomplete").has_value());
}

TEST_F(WorkflowServiceTest, PersistsAndRestores) {
  WorkflowDefinition definition;
  definition.name = "Read inventory";
  WorkflowNode node;
  node.id = "read_1";
  node.kind = WorkflowNodeKind::kToolStep;
  node.label = "Read";
  node.tool = ToolId::FromString("info.read.inventory");
  node.args.Set("query", "all");
  definition.nodes.push_back(std::move(node));
  const WorkflowId id = workflows_.SaveWorkflow(std::move(definition));
  ASSERT_TRUE(id.is_valid());

  WorkflowService restored(&tasks_, FixedClock());
  restored.RestorePersistedState(workflows_.TakePersistedState());
  EXPECT_EQ(restored.size(), 1u);
}

TEST_F(WorkflowServiceTest, PrunesDanglingAndMismatchedSceneReferences) {
  auto make_workflow = [](const std::string& name) {
    WorkflowDefinition definition;
    definition.name = name;
    WorkflowNode node;
    node.id = "read";
    node.kind = WorkflowNodeKind::kToolStep;
    node.label = "Read";
    node.tool = ToolId::FromString("info.read.inventory");
    node.args.Set("query", "all");
    definition.nodes.push_back(std::move(node));
    return definition;
  };

  WorkflowDefinition global = make_workflow("Global");
  const WorkflowId global_id = workflows_.SaveWorkflow(std::move(global));
  ASSERT_TRUE(global_id.is_valid());

  WorkflowDefinition valid = make_workflow("Valid scene");
  valid.scene_scope = "focus";
  valid.trigger.kind = WorkflowTriggerKind::kSceneActivation;
  valid.trigger.scene_id = "focus";
  const WorkflowId valid_id = workflows_.SaveWorkflow(std::move(valid));
  ASSERT_TRUE(valid_id.is_valid());

  WorkflowDefinition missing = make_workflow("Missing scene");
  missing.scene_scope = "gone";
  const WorkflowId missing_id = workflows_.SaveWorkflow(std::move(missing));
  ASSERT_TRUE(missing_id.is_valid());

  WorkflowDefinition mismatch = make_workflow("Mismatched scene");
  mismatch.scene_scope = "focus";
  mismatch.trigger.kind = WorkflowTriggerKind::kSceneActivation;
  mismatch.trigger.scene_id = "other";
  const WorkflowId mismatch_id =
      workflows_.SaveWorkflow(std::move(mismatch));
  ASSERT_TRUE(mismatch_id.is_valid());

  const size_t removed = workflows_.PruneInvalidSceneReferences(
      base::BindRepeating(
          [](const std::string& scene_id) { return scene_id == "focus"; }));
  EXPECT_EQ(removed, 2u);
  EXPECT_TRUE(workflows_.Find(global_id));
  EXPECT_TRUE(workflows_.Find(valid_id));
  EXPECT_FALSE(workflows_.Find(missing_id));
  EXPECT_FALSE(workflows_.Find(mismatch_id));
  EXPECT_EQ(workflows_.PruneInvalidSceneReferences(base::BindRepeating(
                [](const std::string& scene_id) {
                  return scene_id == "focus";
                })),
            0u);
}

TEST_F(WorkflowServiceTest, FullCatalogStillAllowsAtomicReplacement) {
  WorkflowId first;
  for (size_t i = 0; i < kMaxWorkflows; ++i) {
    WorkflowDefinition definition;
    definition.name = "Workflow " + std::to_string(i);
    WorkflowNode node;
    node.id = "read";
    node.kind = WorkflowNodeKind::kToolStep;
    node.label = "Read";
    node.tool = ToolId::FromString("info.read.inventory");
    node.args.Set("query", "all");
    definition.nodes.push_back(std::move(node));
    const WorkflowId id = workflows_.SaveWorkflow(std::move(definition));
    ASSERT_TRUE(id.is_valid());
    if (i == 0) {
      first = id;
    }
  }
  ASSERT_EQ(workflows_.size(), kMaxWorkflows);
  WorkflowDefinition replacement = *workflows_.Find(first);
  replacement.name = "Revised workflow";
  EXPECT_EQ(workflows_.SaveWorkflow(std::move(replacement)), first);
  EXPECT_EQ(workflows_.Find(first)->name, "Revised workflow");
  EXPECT_EQ(workflows_.size(), kMaxWorkflows);
}

} // namespace
} // namespace seoul
