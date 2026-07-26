// Project Seoul product runtime: the workflow service.
// Production owner of saved workflows: store, edit through the typed editor
// operations, compile to a validated plan, run through the task service, and
// build a workflow from a completed task's typed plan (never raw clicks,
// coordinates, or tab indices - the plan is already typed capability calls).
//
// STATE OWNERSHIP
//   owner:        one WorkflowService per profile runtime.
//   lifetime:     the profile runtime.
//   persistence:  serialized via the workflow import/export codec through
//                 TakePersistedState()/Restore (runtime service owns prefs).
//   recovery:     corrupt entries skipped on restore.
//   teardown:     dropped with the runtime.
//   bounds:       kMaxWorkflows.
//   isolation:    per profile.

#ifndef SEOUL_BROWSER_PRODUCT_WORKFLOW_SERVICE_H_
#define SEOUL_BROWSER_PRODUCT_WORKFLOW_SERVICE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "seoul/browser/product/task_service.h"
#include "seoul/browser/tasks/plan_types.h"
#include "seoul/browser/workflows/workflow_editor.h"
#include "seoul/browser/workflows/workflow_graph.h"
#include "seoul/browser/workflows/workflow_types.h"

namespace seoul {

inline constexpr size_t kMaxWorkflows = 200;

class WorkflowService {
 public:
  // `tasks` must outlive this service (the runtime owns both). Edits stamp
  // `modified_at` through `clock`.
  WorkflowService(TaskService* tasks, WorkflowClock clock);
  WorkflowService(const WorkflowService&) = delete;
  WorkflowService& operator=(const WorkflowService&) = delete;
  ~WorkflowService();

  // Stores a validated definition. Returns its id, or invalid on failure.
  WorkflowId SaveWorkflow(WorkflowDefinition definition);
  bool DeleteWorkflow(const WorkflowId& id);
  std::optional<WorkflowId> DuplicateWorkflow(const WorkflowId& id);

  const WorkflowDefinition* Find(const WorkflowId& id) const;
  std::vector<WorkflowId> All() const;

  // Applies one typed edit; the definition revalidates atomically. `node` is
  // inserted after `after_node_id`, or appended when it is empty.
  WorkflowStatusResult AddNode(const WorkflowId& id,
                               WorkflowNode node,
                               const std::string& after_node_id);
  WorkflowStatusResult RemoveNode(const WorkflowId& id,
                                  const std::string& node_id);
  WorkflowStatusResult AddEdge(const WorkflowId& id, WorkflowEdge edge);
  WorkflowStatusResult RemoveEdge(const WorkflowId& id,
                                  const std::string& from,
                                  const std::string& to);

  // Compiles and starts the workflow as a task. Returns the task id, or an
  // invalid id when compilation/validation fails.
  TaskId RunWorkflow(const WorkflowId& id,
                     const LiveWindowKey& window,
                     const ToolPermissionContext& context,
                     bool user_gesture = true);

  // Builds a workflow from a completed task's typed plan. The plan's steps
  // are already registered capability calls; nothing positional is saved.
  std::optional<WorkflowId> SaveTaskAsWorkflow(const TaskId& task_id,
                                               const std::string& name);

  // Import/export through the workflow codec (user-driven).
  std::optional<WorkflowId> Import(const base::Value& value);
  std::optional<base::DictValue> Export(const WorkflowId& id) const;

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue& state);
  // Removes workflows whose Scene scope/activation trigger no longer resolves
  // or whose two Scene references disagree.
  size_t PruneInvalidSceneReferences(
      base::RepeatingCallback<bool(const std::string&)> scene_exists);

  size_t size() const { return workflows_.size(); }

 private:
  raw_ptr<TaskService> tasks_;
  WorkflowClock clock_;
  std::map<WorkflowId, WorkflowDefinition> workflows_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_WORKFLOW_SERVICE_H_
