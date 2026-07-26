// Project Seoul product runtime: the task service.

#include "seoul/browser/product/task_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "seoul/browser/tasks/plan_validator.h"

namespace seoul {

namespace {

constexpr size_t kMaxTaskInputBytes = 2048;
constexpr size_t kMaxPlanningContextBytes = 16 * 1024;

bool InputValueAllowed(const base::Value& value) {
  return value.is_string() || value.is_bool() || value.is_int() ||
         value.is_double();
}

bool ValidTaskInput(const base::DictValue& input, std::string* serialized) {
  if (input.empty() || input.size() > 16) {
    return false;
  }
  for (const auto [key, value] : input) {
    if (key.empty() || key.size() > 80 || !InputValueAllowed(value)) {
      return false;
    }
    if (value.is_string() && value.GetString().size() > kMaxTaskInputBytes) {
      return false;
    }
  }
  base::JSONWriter::Write(input, serialized);
  return !serialized->empty() && serialized->size() <= kMaxTaskInputBytes;
}

}  // namespace

TaskSnapshot::TaskSnapshot() = default;
TaskSnapshot::TaskSnapshot(const TaskSnapshot&) = default;
TaskSnapshot& TaskSnapshot::operator=(const TaskSnapshot&) = default;
TaskSnapshot::TaskSnapshot(TaskSnapshot&&) = default;
TaskSnapshot& TaskSnapshot::operator=(TaskSnapshot&&) = default;
TaskSnapshot::~TaskSnapshot() = default;

TaskService::ActiveTask::ActiveTask() = default;
TaskService::ActiveTask::~ActiveTask() = default;

TaskService::TaskService(ToolRegistry* registry,
                         CapabilityExecutorRegistry* executors,
                         Planner* planner,
                         base::RepeatingCallback<base::Time()> clock,
                         AgentPermissionService* permissions,
                         PermissionScopeResolver permission_scope_resolver)
    : registry_(registry),
      executors_(executors),
      planner_(planner),
      clock_(std::move(clock)),
      permissions_(permissions),
      permission_scope_resolver_(std::move(permission_scope_resolver)) {}

TaskService::~TaskService() = default;

TaskId TaskService::StartTask(const std::string& goal,
                              const LiveWindowKey& window,
                              const ToolPermissionContext& context,
                              bool use_model,
                              bool prefer_local,
                              bool allow_cloud_models,
                              bool user_gesture) {
  if (shutting_down_ || goal.empty() || goal.size() > kMaxGoalLength ||
      tasks_.size() >= kMaxTasksInDeck) {
    return TaskId();
  }
  const TaskId task_id = TaskId::GenerateNew();
  auto task = std::make_unique<ActiveTask>();
  task->goal = goal;
  task->planning_goal = goal;
  task->window = window;
  task->context = context;
  task->use_model = use_model;
  task->prefer_local = prefer_local;
  task->allow_cloud_models = allow_cloud_models;
  task->user_gesture = user_gesture;
  tasks_[task_id] = std::move(task);
  // Planning may involve a provider round trip. Publish immediately so the
  // Task Deck never appears idle while planning is already pending.
  NotifyUpdated(task_id);
  planner_->BuildPlan(goal, context, use_model, prefer_local,
                      allow_cloud_models,
                      base::BindOnce(&TaskService::OnPlanned,
                                     weak_factory_.GetWeakPtr(), task_id));
  return task_id;
}

TaskId TaskService::StartTaskWithPlan(const std::string& goal,
                                      Plan plan,
                                      PlanOrigin origin,
                                      const LiveWindowKey& window,
                                      const ToolPermissionContext& context,
                                      bool user_gesture) {
  if (shutting_down_ || tasks_.size() >= kMaxTasksInDeck) {
    return TaskId();
  }
  // Imported workflows and direct capability callers are untrusted plan
  // sources too. Re-apply descriptor policy here at the final execution
  // boundary so no caller can omit an approval bit and bypass the gate.
  Planner::EnforceApprovalPolicy(plan, *registry_);
  if (!ValidatePlan(plan, *registry_, context).has_value()) {
    return TaskId();
  }
  const TaskId task_id = TaskId::GenerateNew();
  auto task = std::make_unique<ActiveTask>();
  task->goal = goal;
  task->planning_goal = goal;
  task->window = window;
  task->context = context;
  task->user_gesture = user_gesture;
  task->plan_origin = origin;
  task->execution = std::make_unique<TaskExecution>(
      task_id, std::move(plan),
      base::BindRepeating([](ToolRegistry* registry,
                             const ToolId& id) { return registry->Find(id); },
                          registry_.get()),
      clock_);
  tasks_[task_id] = std::move(task);
  tasks_[task_id]->execution->Start();
  Pump(task_id);
  return task_id;
}

void TaskService::OnPlanned(TaskId task_id, PlannerResult result) {
  ActiveTask* task = FindTask(task_id);
  if (!task || shutting_down_) {
    return;
  }
  if (!result.ok) {
    // The task never became executable; represent that as a failed execution
    // with the planner's reason so the deck can show it honestly.
    Plan empty_plan;
    empty_plan.goal = task->goal;
    PlanStep unavailable;
    unavailable.id = "planning";
    unavailable.kind = PlanStepKind::kUserInput;
    unavailable.prompt = result.failure;
    empty_plan.steps.push_back(std::move(unavailable));
    task->execution = std::make_unique<TaskExecution>(
        task_id, std::move(empty_plan),
        base::BindRepeating([](ToolRegistry* registry,
                               const ToolId& id) { return registry->Find(id); },
                            registry_.get()),
        clock_);
    task->execution->Start();
    task->execution->Cancel();
    task->pending_approval_prompt = result.failure;
    NotifyUpdated(task_id);
    FinishNotify(task_id);
    return;
  }
  task->plan_origin = result.origin;
  task->execution = std::make_unique<TaskExecution>(
      task_id, std::move(result.plan),
      base::BindRepeating([](ToolRegistry* registry,
                             const ToolId& id) { return registry->Find(id); },
                          registry_.get()),
      clock_);
  task->execution->Start();
  NotifyUpdated(task_id);
  Pump(task_id);
}

void TaskService::OnReplanned(TaskId task_id, PlannerResult result) {
  ActiveTask* task = FindTask(task_id);
  if (!task || shutting_down_) {
    return;
  }
  if (!result.ok) {
    const std::string failure =
        result.failure.empty()
            ? "The supplied input could not produce a safe plan."
            : result.failure;
    Plan failed_plan;
    failed_plan.goal = task->goal;
    PlanStep unavailable;
    unavailable.id = "input_replanning";
    unavailable.kind = PlanStepKind::kUserInput;
    unavailable.prompt = failure;
    failed_plan.steps.push_back(std::move(unavailable));
    task->execution = std::make_unique<TaskExecution>(
        task_id, std::move(failed_plan),
        base::BindRepeating([](ToolRegistry* registry,
                               const ToolId& id) { return registry->Find(id); },
                            registry_.get()),
        clock_);
    task->execution->Start();
    task->pending_approval_step = "input_replanning";
    task->pending_approval_prompt = failure;
    task->pending_user_input = true;
    NotifyUpdated(task_id);
    return;
  }
  task->plan_origin = result.origin;
  task->execution = std::make_unique<TaskExecution>(
      task_id, std::move(result.plan),
      base::BindRepeating([](ToolRegistry* registry,
                             const ToolId& id) { return registry->Find(id); },
                          registry_.get()),
      clock_);
  task->execution->Start();
  NotifyUpdated(task_id);
  Pump(task_id);
}

void TaskService::Pump(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution || shutting_down_) {
    return;
  }
  // Re-entrancy guard: if a synchronous executor completes a step inside
  // DispatchStep (which runs inside PumpOnce), the resulting OnStepOutcome
  // routes back here. Rather than recurse (unbounded stack for a synchronous
  // multi-step plan, and duplicate terminal notifications), flag a follow-up
  // pass and let the top-level loop below run it.
  if (task->driving) {
    task->pump_pending = true;
    return;
  }
  task->driving = true;
  do {
    task->pump_pending = false;
    PumpOnce(task_id);
    task = FindTask(task_id);
  } while (task && task->pump_pending && !shutting_down_);
  if (task) {
    task->driving = false;
  }
}

void TaskService::PumpOnce(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution || shutting_down_) {
    return;
  }
  while (true) {
    // Start() returns and records the first blocking action. TaskService does
    // not consume that return value directly, so recover an already-recorded
    // approval/input wait before asking Advance() for a new action. Without
    // this, the pending step is skipped by Advance and the task can be
    // misclassified as assumption-invalid.
    NextAction next;
    bool has_recorded_wait = false;
    for (const PlanStep& step : task->execution->plan().steps) {
      const StepStatus status = task->execution->step_status(step.id);
      if (status == StepStatus::kAwaitingApproval) {
        next.kind = NextAction::Kind::kAwaitApproval;
        next.step_id = step.id;
        has_recorded_wait = true;
        break;
      }
      if (status == StepStatus::kAwaitingInput) {
        next.kind = NextAction::Kind::kAwaitInput;
        next.step_id = step.id;
        has_recorded_wait = true;
        break;
      }
    }
    if (!has_recorded_wait) {
      next = task->execution->Advance();
    }
    switch (next.kind) {
      case NextAction::Kind::kRunStep: {
        if (!task->execution->BeginStep(next.step_id)) {
          return;  // not currently eligible; wait for an outcome
        }
        DispatchStep(task_id, next.step_id);
        // Loop again: a read-only parallel group may offer more steps.
        continue;
      }
      case NextAction::Kind::kAwaitApproval: {
        const PlanStep* step = nullptr;
        for (const PlanStep& s : task->execution->plan().steps) {
          if (s.id == next.step_id) {
            step = &s;
            break;
          }
        }
        if (step && step->kind == PlanStepKind::kToolCall && permissions_ &&
            permission_scope_resolver_) {
          const ToolDescriptor* descriptor = registry_->Find(step->tool);
          if (descriptor) {
            AgentPermissionRequest request = permission_scope_resolver_.Run(
                task->window, *descriptor, step->args,
                task->user_gesture);
            const AgentPermissionDecision decision =
                permissions_->Evaluate(request);
            if (decision.kind == AgentPermissionDecisionKind::kAllowed) {
              task->execution->RecordApproval(next.step_id, true);
              continue;
            }
            if (decision.kind == AgentPermissionDecisionKind::kDenied) {
              // Convert a denied dynamic scope into a real failed receipt.
              // Never silently skip and accidentally report task success.
              // This branch is reached only for a step Advance() reported as
              // awaiting approval, so RecordApproval always moves it to pending
              // and BeginStep succeeds here unless a step guard defers it; a
              // guard-deferred step is not stranded, since the next Advance()
              // resolves the guard and re-drives the pump.
              task->execution->RecordApproval(next.step_id, true);
              if (task->execution->BeginStep(next.step_id)) {
                StepOutcome outcome;
                outcome.status = StepStatus::kFailed;
                outcome.observed_summary =
                    "Agent permission scope was invalid or unavailable.";
                HandleDecision(task_id, task->execution->RecordStepOutcome(
                                            next.step_id, outcome));
              }
              return;
            }
            task->pending_permission_request = std::move(request);
          }
        }
        task->pending_approval_step = next.step_id;
        task->pending_user_input = false;
        task->pending_approval_prompt =
            task->pending_permission_request
                ? permissions_->Describe(*task->pending_permission_request)
            : step && !step->prompt.empty()
                ? step->prompt
                : std::string("Approve step ") + next.step_id + "?";
        NotifyUpdated(task_id);
        for (TaskServiceObserver& observer : observers_) {
          observer.OnTaskNeedsApproval(task_id, next.step_id,
                                       task->pending_approval_prompt);
        }
        return;
      }
      case NextAction::Kind::kAwaitInput: {
        task->pending_approval_step = next.step_id;
        task->pending_user_input = true;
        task->pending_approval_prompt = "This task needs more input.";
        NotifyUpdated(task_id);
        return;
      }
      case NextAction::Kind::kWait:
        return;
      case NextAction::Kind::kCompleted:
      case NextAction::Kind::kStopped: {
        if (task->active_dispatch_count > 0) {
          return;  // final state settles when outcomes land
        }
        NotifyUpdated(task_id);
        FinishNotify(task_id);
        return;
      }
    }
  }
}

void TaskService::DispatchStep(const TaskId& task_id,
                               const std::string& step_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task) {
    return;
  }
  const PlanStep* step = nullptr;
  for (const PlanStep& s : task->execution->plan().steps) {
    if (s.id == step_id) {
      step = &s;
      break;
    }
  }
  if (!step || step->kind != PlanStepKind::kToolCall) {
    StepOutcome outcome;
    outcome.status = StepStatus::kFailed;
    outcome.observed_summary = "Step is not executable.";
    task->execution->RecordStepOutcome(step_id, outcome);
    return;
  }
  const ToolDescriptor* descriptor = registry_->Find(step->tool);
  CapabilityExecutor* executor =
      descriptor ? executors_->Find(step->tool, descriptor->version) : nullptr;
  if (!descriptor || !executor) {
    StepOutcome outcome;
    outcome.status = StepStatus::kFailed;
    outcome.observed_summary = "Capability has no registered executor.";
    HandleDecision(task_id,
                   task->execution->RecordStepOutcome(step_id, outcome));
    return;
  }

  ++task->active_dispatch_count;
  CapabilityRequest request;
  request.capability = step->tool;
  request.version = descriptor->version;
  request.args = step->args.Clone();
  request.task_id = task_id;
  request.step_id = step_id;
  request.window = task->window;
  request.user_gesture = task->user_gesture;

  auto timer = std::make_unique<base::OneShotTimer>();
  timer->Start(FROM_HERE, descriptor->timeout,
               base::BindOnce(&TaskService::OnStepTimeout,
                              weak_factory_.GetWeakPtr(), task_id, step_id));
  task->step_timers[step_id] = std::move(timer);

  executor->Execute(
      std::move(request),
      base::BindOnce(&TaskService::OnStepOutcome, weak_factory_.GetWeakPtr(),
                     task_id, step_id));
}

void TaskService::OnStepOutcome(TaskId task_id,
                                std::string step_id,
                                CapabilityOutcome outcome) {
  ActiveTask* task = FindTask(task_id);
  if (!task || shutting_down_) {
    return;
  }
  auto timer_it = task->step_timers.find(step_id);
  if (timer_it == task->step_timers.end()) {
    return;  // timeout already recorded this step; late result is dropped
  }
  task->step_timers.erase(timer_it);
  task->active_dispatch_count = std::max(0, task->active_dispatch_count - 1);

  if (outcome.semantic.has_value()) {
    task->semantic = std::move(outcome.semantic);
  }
  if (outcome.step.status == StepStatus::kOutcomeUnknown) {
    task->had_unknown_outcome_mutation = true;
  }
  HandleDecision(task_id,
                 task->execution->RecordStepOutcome(step_id, outcome.step));
}

void TaskService::OnStepTimeout(TaskId task_id, std::string step_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || shutting_down_) {
    return;
  }
  auto timer_it = task->step_timers.find(step_id);
  if (timer_it == task->step_timers.end()) {
    return;
  }
  task->step_timers.erase(timer_it);
  task->active_dispatch_count = std::max(0, task->active_dispatch_count - 1);

  const PlanStep* step = nullptr;
  for (const PlanStep& s : task->execution->plan().steps) {
    if (s.id == step_id) {
      step = &s;
      break;
    }
  }
  const ToolDescriptor* descriptor =
      step ? registry_->Find(step->tool) : nullptr;
  if (descriptor) {
    CapabilityExecutor* executor =
        executors_->Find(step->tool, descriptor->version);
    if (executor) {
      executor->Cancel(task_id, step_id);
    }
  }
  StepOutcome outcome;
  // A timed-out read is a failure; a timed-out mutation has an unknown
  // outcome and is never automatically replayed.
  const bool mutating =
      descriptor && descriptor->risk != RiskCategory::kReadOnly;
  outcome.status = mutating ? StepStatus::kOutcomeUnknown : StepStatus::kFailed;
  outcome.observed_summary = "Timed out waiting for the capability.";
  if (mutating) {
    task->had_unknown_outcome_mutation = true;
  }
  HandleDecision(task_id, task->execution->RecordStepOutcome(step_id, outcome));
}

void TaskService::HandleDecision(const TaskId& task_id,
                                 ExecutionDecision decision) {
  ActiveTask* task = FindTask(task_id);
  if (!task) {
    return;
  }
  NotifyUpdated(task_id);
  switch (decision) {
    case ExecutionDecision::kContinue:
    case ExecutionDecision::kRetryStep:
      Pump(task_id);
      return;
    case ExecutionDecision::kReplan:
      Replan(task_id);
      return;
    case ExecutionDecision::kAskUser: {
      task->pending_approval_prompt =
          "The task hit a condition it cannot resolve alone.";
      NotifyUpdated(task_id);
      return;
    }
    case ExecutionDecision::kStop:
      Pump(task_id);  // lets the execution surface its terminal state
      FinishNotify(task_id);
      return;
  }
}

void TaskService::Replan(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task) {
    return;
  }
  // A mutation with an unknown outcome is never replayed automatically: the
  // user decides. This is a hard rule, not a policy knob.
  if (task->had_unknown_outcome_mutation) {
    task->pending_approval_prompt =
        "A change may or may not have been applied. Review the receipts "
        "before continuing; nothing was retried automatically.";
    task->execution->Cancel();
    NotifyUpdated(task_id);
    FinishNotify(task_id);
    return;
  }
  if (task->replans_used >= task->execution->plan().budgets.max_replans) {
    task->execution->Cancel();
    NotifyUpdated(task_id);
    FinishNotify(task_id);
    return;
  }
  ++task->replans_used;
  // Preserve the verified evidence: receipts carry across the replan.
  const std::vector<ActionReceipt>& receipts = task->execution->receipts();
  task->carried_receipts.insert(task->carried_receipts.end(), receipts.begin(),
                                receipts.end());
  task->execution->Cancel();
  task->execution.reset();
  NotifyUpdated(task_id);
  // Refresh the live registry through the same configured reasoning route as
  // the original task. Replanning never silently downgrades a model-backed
  // task to lexical deterministic matching.
  planner_->BuildPlan(task->planning_goal, task->context, task->use_model,
                      task->prefer_local, task->allow_cloud_models,
                      base::BindOnce(&TaskService::OnReplanned,
                                     weak_factory_.GetWeakPtr(), task_id));
}

bool TaskService::Approve(const TaskId& task_id,
                          const std::string& step_id,
                          bool approved) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution || task->pending_approval_step != step_id) {
    return false;
  }
  task->pending_approval_step.clear();
  task->pending_approval_prompt.clear();
  task->pending_user_input = false;
  if (approved && task->pending_permission_request && permissions_) {
    // High-risk/always-required requests intentionally refuse a reusable
    // grant; this approval still applies to the current step exactly once.
    permissions_->GrantFirstUse(*task->pending_permission_request);
  }
  task->pending_permission_request.reset();
  task->execution->RecordApproval(step_id, approved);
  NotifyUpdated(task_id);
  Pump(task_id);
  return true;
}

bool TaskService::ProvideInput(const TaskId& task_id,
                               const std::string& step_id,
                               base::DictValue input) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution || task->pending_approval_step != step_id) {
    return false;
  }
  const PlanStep* pending = nullptr;
  for (const PlanStep& step : task->execution->plan().steps) {
    if (step.id == step_id) {
      pending = &step;
      break;
    }
  }
  std::string serialized;
  if (!pending || pending->kind != PlanStepKind::kUserInput ||
      !ValidTaskInput(input, &serialized)) {
    return false;
  }
  const std::string context = task->planning_goal +
                              "\n\nUser-provided input for the pending step " +
                              step_id + ": " + serialized;
  if (context.size() > kMaxPlanningContextBytes ||
      task->replans_used >= task->execution->plan().budgets.max_replans) {
    return false;
  }

  task->execution->RecordUserInput(step_id, std::move(input));
  const std::vector<ActionReceipt>& receipts = task->execution->receipts();
  task->carried_receipts.insert(task->carried_receipts.end(), receipts.begin(),
                                receipts.end());
  task->execution->Cancel();
  task->execution.reset();
  task->planning_goal = context;
  ++task->replans_used;
  task->pending_approval_step.clear();
  task->pending_approval_prompt.clear();
  task->pending_user_input = false;
  NotifyUpdated(task_id);
  planner_->BuildPlan(task->planning_goal, task->context, task->use_model,
                      task->prefer_local, task->allow_cloud_models,
                      base::BindOnce(&TaskService::OnReplanned,
                                     weak_factory_.GetWeakPtr(), task_id));
  return true;
}

bool TaskService::Pause(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution) {
    return false;
  }
  task->execution->Pause();
  NotifyUpdated(task_id);
  return true;
}

bool TaskService::Resume(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution) {
    return false;
  }
  task->execution->ResumeFromPause();
  NotifyUpdated(task_id);
  Pump(task_id);
  return true;
}

bool TaskService::Cancel(const TaskId& task_id) {
  ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution) {
    return false;
  }
  for (auto& [step_id, timer] : task->step_timers) {
    const PlanStep* step = nullptr;
    for (const PlanStep& s : task->execution->plan().steps) {
      if (s.id == step_id) {
        step = &s;
        break;
      }
    }
    if (step) {
      const ToolDescriptor* descriptor = registry_->Find(step->tool);
      CapabilityExecutor* executor =
          descriptor ? executors_->Find(step->tool, descriptor->version)
                     : nullptr;
      if (executor) {
        executor->Cancel(task_id, step_id);
      }
    }
  }
  task->step_timers.clear();
  task->active_dispatch_count = 0;
  task->execution->Cancel();
  NotifyUpdated(task_id);
  FinishNotify(task_id);
  return true;
}

std::vector<TaskSnapshot> TaskService::Snapshots() const {
  std::vector<TaskSnapshot> out;
  out.reserve(tasks_.size());
  for (const auto& [id, task] : tasks_) {
    if (auto snapshot = Snapshot(id); snapshot.has_value()) {
      out.push_back(std::move(snapshot.value()));
    }
  }
  return out;
}

std::vector<TaskStateSummary> TaskService::StateSummaries() const {
  std::vector<TaskStateSummary> out;
  out.reserve(tasks_.size());
  for (const auto& [id, task] : tasks_) {
    (void)id;
    TaskStateSummary summary;
    summary.window = task->window;
    summary.state = EffectiveState(*task);
    out.push_back(summary);
  }
  return out;
}

TaskState TaskService::EffectiveState(const ActiveTask& task) {
  if (!task.execution) {
    return TaskState::kPlanning;
  }
  if (!task.pending_approval_step.empty()) {
    return TaskState::kAwaitingApproval;
  }
  return task.execution->state();
}

std::optional<TaskSnapshot> TaskService::Snapshot(const TaskId& task_id) const {
  const ActiveTask* task = FindTask(task_id);
  if (!task) {
    return std::nullopt;
  }
  TaskSnapshot snapshot;
  snapshot.id = task_id;
  snapshot.goal = task->goal;
  snapshot.window = task->window;
  snapshot.plan_origin = task->plan_origin;
  snapshot.replans_used = task->replans_used;
  snapshot.state = EffectiveState(*task);
  snapshot.pending_approval_step = task->pending_approval_step;
  snapshot.pending_approval_prompt = task->pending_approval_prompt;
  snapshot.pending_user_input = task->pending_user_input;
  snapshot.has_semantic_result = task->semantic.has_value();
  snapshot.receipts = task->carried_receipts;
  if (task->execution) {
    snapshot.failure = task->execution->failure_reason();
    snapshot.usage = task->execution->usage();
    const std::vector<ActionReceipt>& receipts = task->execution->receipts();
    snapshot.receipts.insert(snapshot.receipts.end(), receipts.begin(),
                             receipts.end());
    if (snapshot.receipts.size() > kMaxReceiptsPerTask) {
      snapshot.receipts.erase(snapshot.receipts.begin(),
                              snapshot.receipts.end() - kMaxReceiptsPerTask);
    }
  }
  return snapshot;
}

const SemanticResult* TaskService::FinalSemanticResult(
    const TaskId& task_id) const {
  const ActiveTask* task = FindTask(task_id);
  return task && task->semantic.has_value() ? &task->semantic.value() : nullptr;
}

std::optional<Plan> TaskService::PlanOf(const TaskId& task_id) const {
  const ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution) {
    return std::nullopt;
  }
  return task->execution->plan();
}

const ToolPermissionContext* TaskService::ContextOf(
    const TaskId& task_id) const {
  const ActiveTask* task = FindTask(task_id);
  return task ? &task->context : nullptr;
}

void TaskService::AddObserver(TaskServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void TaskService::RemoveObserver(TaskServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void TaskService::Shutdown() {
  shutting_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  for (auto& [id, task] : tasks_) {
    task->step_timers.clear();
    if (task->execution &&
        (task->execution->state() == TaskState::kExecuting ||
         task->execution->state() == TaskState::kAwaitingApproval ||
         task->execution->state() == TaskState::kPaused)) {
      task->execution->Cancel();
    }
  }
}

void TaskService::NotifyUpdated(const TaskId& task_id) {
  if (shutting_down_) {
    return;
  }
  for (TaskServiceObserver& observer : observers_) {
    observer.OnTaskUpdated(task_id);
  }
}

void TaskService::FinishNotify(const TaskId& task_id) {
  if (shutting_down_) {
    return;
  }
  const ActiveTask* task = FindTask(task_id);
  if (!task || !task->execution) {
    return;
  }
  ActiveTask* mutable_task = FindTask(task_id);
  const TaskState state = task->execution->state();
  const bool terminal = state == TaskState::kCompleted ||
                        state == TaskState::kFailed ||
                        state == TaskState::kCancelled;
  if (!terminal || !mutable_task || mutable_task->finished_notified) {
    return;
  }
  // OnTaskFinished fires exactly once per task, even though several drive
  // paths may observe the terminal state.
  mutable_task->finished_notified = true;
  for (TaskServiceObserver& observer : observers_) {
    observer.OnTaskFinished(task_id);
  }
}

TaskService::ActiveTask* TaskService::FindTask(const TaskId& task_id) {
  auto it = tasks_.find(task_id);
  return it != tasks_.end() ? it->second.get() : nullptr;
}

const TaskService::ActiveTask* TaskService::FindTask(
    const TaskId& task_id) const {
  auto it = tasks_.find(task_id);
  return it != tasks_.end() ? it->second.get() : nullptr;
}

}  // namespace seoul
