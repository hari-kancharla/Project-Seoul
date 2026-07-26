// Project Seoul product runtime: the task service.
// Owns every live task: planning, validated execution through the capability
// executor registry, approvals, pause/resume/cancel, per-step timeouts,
// bounded replanning, receipts, and the final semantic result. This is the
// production owner the Task Deck and Canvas render from.
//
// STATE OWNERSHIP
//   owner:        one TaskService per profile runtime.
//   lifetime:     the profile runtime.
//   persistence:  none in V1 beyond checkpoints handed to callers; the deck
//                 is rebuilt per session. Receipts are bounded per task.
//   recovery:     a task interrupted by shutdown reports kOutcomeUnknown for
//                 in-progress mutations; nothing replays automatically.
//   teardown:     Shutdown() cancels executors, stops timers, and drops
//                 tasks; no callback runs after shutdown.
//   bounds:       kMaxTasksInDeck live+finished tasks; per-task budgets.
//   isolation:    per profile; never process-global.

#ifndef SEOUL_BROWSER_PRODUCT_TASK_SERVICE_H_
#define SEOUL_BROWSER_PRODUCT_TASK_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "seoul/browser/product/capability_executor.h"
#include "seoul/browser/product/planner.h"
#include "seoul/browser/policy/agent_permission_service.h"
#include "seoul/browser/tasks/task_execution.h"
#include "seoul/browser/tools/tool_registry.h"

namespace seoul {

class TaskServiceObserver : public base::CheckedObserver {
 public:
  // Any state, receipt, or step change worth re-rendering.
  virtual void OnTaskUpdated(const TaskId& task_id) = 0;
  // A step needs explicit user approval before it runs.
  virtual void OnTaskNeedsApproval(const TaskId& task_id,
                                   const std::string& step_id,
                                   const std::string& prompt) {}
  // Terminal transition (completed, failed, cancelled).
  virtual void OnTaskFinished(const TaskId& task_id) {}
};

// A read snapshot for rendering. Receipts are copied bounded.
struct TaskSnapshot {
  TaskSnapshot();
  TaskSnapshot(const TaskSnapshot&);
  TaskSnapshot& operator=(const TaskSnapshot&);
  TaskSnapshot(TaskSnapshot&&);
  TaskSnapshot& operator=(TaskSnapshot&&);
  ~TaskSnapshot();

  TaskId id;
  std::string goal;
  TaskState state = TaskState::kDraft;
  TaskFailureReason failure = TaskFailureReason::kNone;
  PlanOrigin plan_origin = PlanOrigin::kDeterministic;
  std::vector<ActionReceipt> receipts;
  BudgetUsage usage;
  std::string pending_approval_step;    // non-empty while awaiting approval
  std::string pending_approval_prompt;  // what to show the user
  bool pending_user_input = false;
  bool has_semantic_result = false;
  LiveWindowKey window;
  int replans_used = 0;
};

// Lean status projection for always-visible chrome. It deliberately excludes
// goals, prompts, receipts, semantic results, and usage so frequent shell
// updates do not copy task payloads.
struct TaskStateSummary {
  LiveWindowKey window;
  TaskState state = TaskState::kDraft;
};

class TaskService {
 public:
  using PermissionScopeResolver = base::RepeatingCallback<AgentPermissionRequest(
      const LiveWindowKey&,
      const ToolDescriptor&,
      const base::DictValue&,
      bool user_gesture)>;

  // All raw pointers must outlive this service (the runtime owns them all).
  TaskService(ToolRegistry* registry,
              CapabilityExecutorRegistry* executors,
              Planner* planner,
              base::RepeatingCallback<base::Time()> clock,
              AgentPermissionService* permissions = nullptr,
              PermissionScopeResolver permission_scope_resolver = {});
  TaskService(const TaskService&) = delete;
  TaskService& operator=(const TaskService&) = delete;
  ~TaskService();

  // Plans (model-backed when requested and available, deterministic
  // otherwise) and starts executing. Returns the task id immediately; state
  // streams through observers.
  TaskId StartTask(const std::string& goal,
                   const LiveWindowKey& window,
                   const ToolPermissionContext& context,
                   bool use_model,
                   bool prefer_local,
                   bool allow_cloud_models = true,
                   bool user_gesture = true);

  // Starts a task from an already-validated plan (workflow runs). The caller
  // must have run ValidatePlan; this re-validates defensively.
  TaskId StartTaskWithPlan(const std::string& goal,
                           Plan plan,
                           PlanOrigin origin,
                           const LiveWindowKey& window,
                           const ToolPermissionContext& context,
                           bool user_gesture = true);

  // Approval response for the step named in OnTaskNeedsApproval. A rejection
  // skips the gated step and continues the rest of the plan.
  bool Approve(const TaskId& task_id,
               const std::string& step_id,
               bool approved);
  // Supplies bounded typed input for the exact pending kUserInput step, then
  // replans with that user-owned context. Input is never guessed, silently
  // discarded, or applied to a different step.
  bool ProvideInput(const TaskId& task_id,
                    const std::string& step_id,
                    base::DictValue input);

  bool Pause(const TaskId& task_id);
  bool Resume(const TaskId& task_id);
  bool Cancel(const TaskId& task_id);

  std::vector<TaskSnapshot> Snapshots() const;
  std::vector<TaskStateSummary> StateSummaries() const;
  std::optional<TaskSnapshot> Snapshot(const TaskId& task_id) const;
  // Present when the task produced user-facing data (last semantic outcome).
  const SemanticResult* FinalSemanticResult(const TaskId& task_id) const;
  // The executed plan, for save-as-workflow. Empty optional for unknown ids.
  std::optional<Plan> PlanOf(const TaskId& task_id) const;
  const ToolPermissionContext* ContextOf(const TaskId& task_id) const;

  void AddObserver(TaskServiceObserver* observer);
  void RemoveObserver(TaskServiceObserver* observer);

  // Cancels everything; after this no callback fires and no observer is
  // notified. Called by the runtime service during profile shutdown.
  void Shutdown();

  size_t task_count() const { return tasks_.size(); }

 private:
  struct ActiveTask {
    ActiveTask();
    ~ActiveTask();

    std::string goal;
    std::string planning_goal;
    LiveWindowKey window;
    ToolPermissionContext context;
    PlanOrigin plan_origin = PlanOrigin::kDeterministic;
    std::unique_ptr<TaskExecution> execution;
    std::vector<ActionReceipt> carried_receipts;  // receipts across replans
    std::optional<SemanticResult> semantic;
    std::string pending_approval_step;
    std::string pending_approval_prompt;
    bool pending_user_input = false;
    std::optional<AgentPermissionRequest> pending_permission_request;
    int replans_used = 0;
    int active_dispatch_count = 0;
    bool use_model = false;
    bool prefer_local = false;
    bool allow_cloud_models = true;
    bool user_gesture = true;
    bool had_unknown_outcome_mutation = false;
    // Re-entrancy guard: a synchronous executor completes its step inside
    // DispatchStep, which would otherwise recurse back into the drive loop.
    // While `driving`, a nested drive request only sets `pump_pending`, and the
    // top-level loop re-runs once more. `finished_notified` makes the terminal
    // OnTaskFinished fire exactly once.
    bool driving = false;
    bool pump_pending = false;
    bool finished_notified = false;
    std::map<std::string, std::unique_ptr<base::OneShotTimer>> step_timers;
  };

  static TaskState EffectiveState(const ActiveTask& task);

  void OnPlanned(TaskId task_id, PlannerResult result);
  // Installs a newly validated plan after either bounded failure recovery or
  // explicit user input. A failed plan becomes a visible input request rather
  // than an invented fallback action.
  void OnReplanned(TaskId task_id, PlannerResult result);
  // Drives the task to its next blocking point (approval/input/in-progress) or
  // to completion, re-entrancy-safe. Dispatches every offered runnable step.
  void Pump(const TaskId& task_id);
  // One pass of the drive loop; only ever called from Pump.
  void PumpOnce(const TaskId& task_id);
  void DispatchStep(const TaskId& task_id, const std::string& step_id);
  void OnStepOutcome(TaskId task_id,
                     std::string step_id,
                     CapabilityOutcome outcome);
  void OnStepTimeout(TaskId task_id, std::string step_id);
  void HandleDecision(const TaskId& task_id, ExecutionDecision decision);
  void Replan(const TaskId& task_id);
  void NotifyUpdated(const TaskId& task_id);
  void FinishNotify(const TaskId& task_id);
  ActiveTask* FindTask(const TaskId& task_id);
  const ActiveTask* FindTask(const TaskId& task_id) const;

  raw_ptr<ToolRegistry> registry_;
  raw_ptr<CapabilityExecutorRegistry> executors_;
  raw_ptr<Planner> planner_;
  base::RepeatingCallback<base::Time()> clock_;
  raw_ptr<AgentPermissionService> permissions_ = nullptr;
  PermissionScopeResolver permission_scope_resolver_;
  std::map<TaskId, std::unique_ptr<ActiveTask>> tasks_;
  base::ObserverList<TaskServiceObserver> observers_;
  bool shutting_down_ = false;
  base::WeakPtrFactory<TaskService> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_TASK_SERVICE_H_
