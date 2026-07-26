// Project Seoul product runtime: the workflow service.

#include "seoul/browser/product/workflow_service.h"

#include <utility>

#include "seoul/browser/workflows/workflow_editor.h"

namespace seoul {

WorkflowService::WorkflowService(TaskService *tasks, WorkflowClock clock)
    : tasks_(tasks), clock_(std::move(clock)) {}

WorkflowService::~WorkflowService() = default;

WorkflowId WorkflowService::SaveWorkflow(WorkflowDefinition definition) {
  if (!ValidateWorkflowStructure(definition).has_value()) {
    return WorkflowId();
  }
  if (!definition.id.is_valid()) {
    definition.id = WorkflowId::GenerateNew();
  }
  const bool replacing = workflows_.contains(definition.id);
  if (!replacing && workflows_.size() >= kMaxWorkflows) {
    return WorkflowId();
  }
  const WorkflowId id = definition.id;
  workflows_[id] = std::move(definition);
  return id;
}

bool WorkflowService::DeleteWorkflow(const WorkflowId &id) {
  return workflows_.erase(id) > 0;
}

std::optional<WorkflowId>
WorkflowService::DuplicateWorkflow(const WorkflowId &id) {
  auto it = workflows_.find(id);
  if (it == workflows_.end() || workflows_.size() >= kMaxWorkflows) {
    return std::nullopt;
  }
  WorkflowDefinition copy = seoul::DuplicateWorkflow(it->second, clock_);
  if (!ValidateWorkflowStructure(copy).has_value()) {
    return std::nullopt;
  }
  const WorkflowId new_id = copy.id;
  workflows_[new_id] = std::move(copy);
  return new_id;
}

const WorkflowDefinition *WorkflowService::Find(const WorkflowId &id) const {
  auto it = workflows_.find(id);
  return it != workflows_.end() ? &it->second : nullptr;
}

std::vector<WorkflowId> WorkflowService::All() const {
  std::vector<WorkflowId> out;
  out.reserve(workflows_.size());
  for (const auto &[id, definition] : workflows_) {
    out.push_back(id);
  }
  return out;
}

WorkflowStatusResult
WorkflowService::AddNode(const WorkflowId &id, WorkflowNode node,
                         const std::string &after_node_id) {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return base::unexpected(WorkflowError::kUnknownNode);
  }
  return AddWorkflowNode(it->second, std::move(node), after_node_id, clock_);
}

WorkflowStatusResult WorkflowService::RemoveNode(const WorkflowId &id,
                                                 const std::string &node_id) {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return base::unexpected(WorkflowError::kUnknownNode);
  }
  return RemoveWorkflowNode(it->second, node_id, clock_);
}

WorkflowStatusResult WorkflowService::AddEdge(const WorkflowId &id,
                                              WorkflowEdge edge) {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return base::unexpected(WorkflowError::kUnknownNode);
  }
  return AddWorkflowEdge(it->second, edge, clock_);
}

WorkflowStatusResult WorkflowService::RemoveEdge(const WorkflowId &id,
                                                 const std::string &from,
                                                 const std::string &to) {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return base::unexpected(WorkflowError::kUnknownNode);
  }
  return RemoveWorkflowEdge(it->second, from, to, clock_);
}

TaskId WorkflowService::RunWorkflow(const WorkflowId &id,
                                    const LiveWindowKey &window,
                                    const ToolPermissionContext &context,
                                    bool user_gesture) {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return TaskId();
  }
  WorkflowResult<Plan> plan =
      CompileWorkflow(it->second, base::DictValue(), TaskBudgets());
  if (!plan.has_value()) {
    return TaskId();
  }
  return tasks_->StartTaskWithPlan(it->second.name, std::move(plan.value()),
                                   PlanOrigin::kDeterministic, window, context,
                                   user_gesture);
}

std::optional<WorkflowId>
WorkflowService::SaveTaskAsWorkflow(const TaskId &task_id,
                                    const std::string &name) {
  if (name.empty() || workflows_.size() >= kMaxWorkflows) {
    return std::nullopt;
  }
  const std::optional<TaskSnapshot> snapshot = tasks_->Snapshot(task_id);
  if (!snapshot.has_value() || snapshot->state != TaskState::kCompleted) {
    return std::nullopt;
  }
  const std::optional<Plan> plan = tasks_->PlanOf(task_id);
  if (!plan.has_value() || plan->steps.empty()) {
    return std::nullopt;
  }
  WorkflowDefinition definition;
  definition.id = WorkflowId::GenerateNew();
  definition.name = name;
  definition.description = plan->goal;
  std::string previous_node;
  for (const PlanStep &step : plan->steps) {
    WorkflowNode node;
    node.id = step.id;
    node.label =
        step.kind == PlanStepKind::kToolCall ? step.tool.value() : step.prompt;
    switch (step.kind) {
    case PlanStepKind::kToolCall:
      node.kind = WorkflowNodeKind::kToolStep;
      node.tool = step.tool;
      node.args = step.args.Clone();
      node.requires_approval = step.requires_approval;
      break;
    case PlanStepKind::kApprovalGate:
      node.kind = WorkflowNodeKind::kApproval;
      node.prompt = step.prompt;
      break;
    case PlanStepKind::kUserInput:
      node.kind = WorkflowNodeKind::kUserInput;
      node.prompt = step.prompt;
      break;
    }
    definition.nodes.push_back(std::move(node));
    if (!previous_node.empty()) {
      WorkflowEdge edge;
      edge.kind = WorkflowEdgeKind::kSequence;
      edge.from = previous_node;
      edge.to = step.id;
      definition.edges.push_back(std::move(edge));
    }
    previous_node = step.id;
  }
  if (!ValidateWorkflowStructure(definition).has_value()) {
    return std::nullopt;
  }
  const WorkflowId id = definition.id;
  workflows_[id] = std::move(definition);
  return id;
}

std::optional<WorkflowId> WorkflowService::Import(const base::Value &value) {
  if (workflows_.size() >= kMaxWorkflows) {
    return std::nullopt;
  }
  WorkflowResult<WorkflowDefinition> imported = ImportWorkflow(value);
  if (!imported.has_value()) {
    return std::nullopt;
  }
  WorkflowDefinition definition = std::move(imported.value());
  definition.id = WorkflowId::GenerateNew(); // imported ids never collide
  const WorkflowId id = definition.id;
  workflows_[id] = std::move(definition);
  return id;
}

std::optional<base::DictValue>
WorkflowService::Export(const WorkflowId &id) const {
  auto it = workflows_.find(id);
  if (it == workflows_.end()) {
    return std::nullopt;
  }
  return ExportWorkflow(it->second);
}

base::DictValue WorkflowService::TakePersistedState() const {
  base::DictValue state;
  base::ListValue workflows;
  for (const auto &[id, definition] : workflows_) {
    workflows.Append(ExportWorkflow(definition));
  }
  state.Set("workflows", std::move(workflows));
  return state;
}

void WorkflowService::RestorePersistedState(const base::DictValue &state) {
  const base::ListValue *workflows = state.FindList("workflows");
  if (!workflows) {
    return;
  }
  for (const base::Value &entry : *workflows) {
    if (workflows_.size() >= kMaxWorkflows) {
      return;
    }
    WorkflowResult<WorkflowDefinition> imported = ImportWorkflow(entry);
    if (!imported.has_value()) {
      continue; // corrupt persisted workflow: skipped
    }
    WorkflowDefinition definition = std::move(imported.value());
    if (!definition.id.is_valid()) {
      definition.id = WorkflowId::GenerateNew();
    }
    const WorkflowId id = definition.id;
    workflows_[id] = std::move(definition);
  }
}

size_t WorkflowService::PruneInvalidSceneReferences(
    base::RepeatingCallback<bool(const std::string&)> scene_exists) {
  size_t removed = 0;
  for (auto it = workflows_.begin(); it != workflows_.end();) {
    const WorkflowDefinition& workflow = it->second;
    const bool scoped_scene_missing =
        !workflow.scene_scope.empty() &&
        (scene_exists.is_null() ||
         !scene_exists.Run(workflow.scene_scope));
    const bool activation_scene_missing =
        workflow.trigger.kind == WorkflowTriggerKind::kSceneActivation &&
        (workflow.trigger.scene_id.empty() || scene_exists.is_null() ||
         !scene_exists.Run(workflow.trigger.scene_id));
    const bool activation_scope_mismatch =
        workflow.trigger.kind == WorkflowTriggerKind::kSceneActivation &&
        !workflow.scene_scope.empty() &&
        workflow.scene_scope != workflow.trigger.scene_id;
    if (!scoped_scene_missing && !activation_scene_missing &&
        !activation_scope_mismatch) {
      ++it;
      continue;
    }
    it = workflows_.erase(it);
    ++removed;
  }
  return removed;
}

} // namespace seoul
