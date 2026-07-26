// Project Seoul workflow system.
// A workflow is a versioned typed task graph over registered tools, not a
// recorded click macro. Nodes carry semantic tool arguments (never raw
// coordinates or tab indices); conditions are observable success/failure
// branches; loops are explicitly bounded; triggers are inspectable. Editing
// happens through typed operations that bump the version.

#ifndef SEOUL_BROWSER_WORKFLOWS_WORKFLOW_TYPES_H_
#define SEOUL_BROWSER_WORKFLOWS_WORKFLOW_TYPES_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "base/values.h"
#include "seoul/browser/tools/tool_schema.h"
#include "seoul/browser/tools/tool_types.h"

namespace seoul {

class WorkflowId {
public:
  WorkflowId() = default;
  static WorkflowId GenerateNew() {
    WorkflowId id;
    id.uuid_ = base::Uuid::GenerateRandomV4();
    return id;
  }
  static WorkflowId FromString(std::string_view s) {
    WorkflowId id;
    id.uuid_ = base::Uuid::ParseLowercase(s);
    return id;
  }
  bool is_valid() const { return uuid_.is_valid(); }
  std::string value() const {
    return uuid_.is_valid() ? uuid_.AsLowercaseString() : std::string();
  }
  friend bool operator==(const WorkflowId &a, const WorkflowId &b) {
    return a.uuid_ == b.uuid_;
  }
  friend bool operator<(const WorkflowId &a, const WorkflowId &b) {
    return a.uuid_ < b.uuid_;
  }

private:
  base::Uuid uuid_;
};

inline constexpr int kWorkflowSchemaVersion = 1;
inline constexpr size_t kMaxWorkflowNodes = 100;
inline constexpr size_t kMaxWorkflowEdges = 200;
inline constexpr size_t kMaxWorkflowParams = 32;
inline constexpr size_t kMaxWorkflowNameLength = 200;
inline constexpr size_t kMaxWorkflowDescriptionLength = 2000;
inline constexpr int kMaxWorkflowLoopIterations = 25;
inline constexpr int kMinTriggerIntervalMinutes = 1;
inline constexpr int kMaxTriggerIntervalMinutes = 10080; // one week

enum class WorkflowNodeKind {
  kToolStep,
  kApproval,  // explicit user approval point
  kUserInput, // collect parameter values mid-run
};

// base::Value and base::DictValue are move-only; WorkflowNode and
// WorkflowParam hold them by value and declare clone-based copy semantics so
// WorkflowDefinition (composed of them) is copyable for edit-then-validate.
struct WorkflowNode {
  WorkflowNode();
  WorkflowNode(const WorkflowNode &);
  WorkflowNode(WorkflowNode &&);
  WorkflowNode &operator=(const WorkflowNode &);
  WorkflowNode &operator=(WorkflowNode &&);
  ~WorkflowNode();

  std::string id; // [a-z][a-z0-9_-]{0,63}, unique within the workflow
  WorkflowNodeKind kind = WorkflowNodeKind::kToolStep;
  std::string label;              // user-visible node name on the canvas
  ToolId tool;                    // kToolStep
  base::DictValue args;           // kToolStep; may reference params (see below)
  std::string prompt;             // kApproval / kUserInput
  bool requires_approval = false; // extra gate on a tool step
  // Bounded loop header: > 0 marks this node as a loop target with this many
  // maximum iterations (a kLoopBack edge must point here).
  int max_iterations = 0;

  friend bool operator==(const WorkflowNode &, const WorkflowNode &) = default;
};

// Conditions are observable outcome branches, not an expression language: a
// predicate ("is the price below 500?") is itself a tool step whose success
// or failure drives kOnSuccess / kOnFailure edges.
enum class WorkflowEdgeKind {
  kSequence,  // run target after source, regardless of outcome
  kOnSuccess, // run target only if source succeeded
  kOnFailure, // run target only if source failed
  kLoopBack,  // return to a bounded loop header
};

struct WorkflowEdge {
  std::string from;
  std::string to;
  WorkflowEdgeKind kind = WorkflowEdgeKind::kSequence;

  friend bool operator==(const WorkflowEdge &, const WorkflowEdge &) = default;
};

enum class WorkflowTriggerKind {
  kManual,
  kSchedule, // fixed interval; every run appears in the Task Deck
  kSceneActivation,
  kNavigation,      // navigation to an allowed origin pattern
  kPageStateChange, // a monitored page condition reported by observation
  kServiceEvent,    // connected-service event by subscription name
  kStartup,
};

struct WorkflowTrigger {
  WorkflowTrigger();
  WorkflowTrigger(const WorkflowTrigger &);
  WorkflowTrigger(WorkflowTrigger &&);
  WorkflowTrigger &operator=(const WorkflowTrigger &);
  WorkflowTrigger &operator=(WorkflowTrigger &&);
  ~WorkflowTrigger();
  WorkflowTriggerKind kind = WorkflowTriggerKind::kManual;
  int interval_minutes = 0;   // kSchedule
  std::string scene_id;       // kSceneActivation
  std::string origin_pattern; // kNavigation / kPageStateChange
  std::string event_name;     // kServiceEvent

  friend bool operator==(const WorkflowTrigger &,
                         const WorkflowTrigger &) = default;
};

// Declared workflow parameter. Node args reference a parameter with the
// placeholder string "{{param:<name>}}"; the compiler substitutes the typed
// run value.
struct WorkflowParam {
  WorkflowParam();
  WorkflowParam(const WorkflowParam &);
  WorkflowParam(WorkflowParam &&);
  WorkflowParam &operator=(const WorkflowParam &);
  WorkflowParam &operator=(WorkflowParam &&);
  ~WorkflowParam();

  SchemaField field;
  base::Value default_value;

  friend bool operator==(const WorkflowParam &,
                         const WorkflowParam &) = default;
};

struct WorkflowRunSummary {
  base::Time ran_at;
  bool succeeded = false;
  std::string detail;

  friend bool operator==(const WorkflowRunSummary &,
                         const WorkflowRunSummary &) = default;
};

struct WorkflowDefinition {
  WorkflowDefinition();
  WorkflowDefinition(const WorkflowDefinition &);
  WorkflowDefinition(WorkflowDefinition &&);
  WorkflowDefinition &operator=(const WorkflowDefinition &);
  WorkflowDefinition &operator=(WorkflowDefinition &&);
  ~WorkflowDefinition();
  WorkflowId id;
  std::string name;
  std::string description;
  std::vector<WorkflowParam> params;
  std::vector<WorkflowNode> nodes; // canvas order; execution order comes
                                   // from edges (deterministic topological)
  std::vector<WorkflowEdge> edges;
  WorkflowTrigger trigger;
  std::string scene_scope; // optional Scene the workflow belongs to
  std::string site_scope;  // optional origin pattern scope
  int version = 1;
  base::Time created_at;
  base::Time updated_at;
  std::optional<WorkflowRunSummary> last_run;

  friend bool operator==(const WorkflowDefinition &,
                         const WorkflowDefinition &) = default;
};

enum class WorkflowError {
  kInvalidName,
  kEmptyWorkflow,
  kTooManyNodes,
  kTooManyEdges,
  kTooManyParams,
  kInvalidNodeId,
  kDuplicateNodeId,
  kUnknownNode,
  kMissingPrompt,
  kEdgeUnknownNode,
  kSelfEdge,
  kDuplicateEdge,
  kCycleWithoutLoop,
  kLoopBackNotToHeader,
  kLoopUnbounded,
  kLoopTooLarge,
  kLoopHeaderUnreachable,
  kInvalidTrigger,
  kUnknownParamReference,
  kInvalidParam,
  kUnsupportedSchema,
  kUnknownTool,
  kArgsInvalid,
  kUnknownWorkflow,
  kLimitExceeded,
  kInUse,
};

const char *WorkflowErrorToString(WorkflowError error);

template <typename T> using WorkflowResult = base::expected<T, WorkflowError>;

using WorkflowStatusResult = base::expected<void, WorkflowError>;

} // namespace seoul

#endif // SEOUL_BROWSER_WORKFLOWS_WORKFLOW_TYPES_H_
