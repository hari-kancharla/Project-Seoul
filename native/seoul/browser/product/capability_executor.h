// Project Seoul product runtime: capability execution.
// One executor per registered capability, keyed by stable capability id and
// version. There is no central action switch: a capability descriptor without
// a registered executor is unavailable to the planner, and an executor without
// a descriptor fails registration. Each executor owns argument decoding,
// validation, permission checks, execution, cancellation, observation,
// verification, error conversion, and receipt fields for its one capability.

#ifndef SEOUL_BROWSER_PRODUCT_CAPABILITY_EXECUTOR_H_
#define SEOUL_BROWSER_PRODUCT_CAPABILITY_EXECUTOR_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/semantic/semantic_types.h"
#include "seoul/browser/tasks/task_execution.h"
#include "seoul/browser/tasks/task_types.h"
#include "seoul/browser/tools/tool_types.h"

namespace seoul {

// One capability invocation. Arguments were validated against the capability's
// input schema by the plan validator; executors re-decode defensively and
// reject anything malformed rather than guessing.
struct CapabilityRequest {
  CapabilityRequest();
  CapabilityRequest(const CapabilityRequest &) = delete;
  CapabilityRequest &operator=(const CapabilityRequest &) = delete;
  CapabilityRequest(CapabilityRequest &&);
  CapabilityRequest &operator=(CapabilityRequest &&);
  ~CapabilityRequest();

  ToolId capability;
  int version = 1;
  base::DictValue args;
  TaskId task_id;
  std::string step_id;
  // The window the task was started from; window-scoped capabilities act here.
  LiveWindowKey window;
  // True only for an explicit user-started turn/run. Automated triggers carry
  // false, so routing rules that require a gesture cannot launch external
  // effects from background automation.
  bool user_gesture = false;
};

// The observed, verified result of one capability run. `semantic` is present
// for capabilities that produce user-facing data; it feeds the adaptive
// interface compiler. StepOutcome carries the receipt fields.
struct CapabilityOutcome {
  CapabilityOutcome();
  CapabilityOutcome(const CapabilityOutcome &) = delete;
  CapabilityOutcome &operator=(const CapabilityOutcome &) = delete;
  CapabilityOutcome(CapabilityOutcome &&);
  CapabilityOutcome &operator=(CapabilityOutcome &&);
  ~CapabilityOutcome();

  StepOutcome step;
  std::optional<SemanticResult> semantic;
};

using CapabilityCallback = base::OnceCallback<void(CapabilityOutcome)>;

class CapabilityExecutor {
public:
  virtual ~CapabilityExecutor() = default;

  // The capability this executor implements. Must match a registered
  // descriptor exactly (id and version).
  virtual ToolId capability_id() const = 0;
  virtual int version() const;

  // Starts one invocation. The callback is invoked exactly once, on the
  // calling sequence, including for rejection, cancellation, timeout, and
  // unknown outcome. It must not be invoked after Cancel unless the outcome
  // was already committed (in which case status is the truthful final state).
  virtual void Execute(CapabilityRequest request,
                       CapabilityCallback callback) = 0;

  // Best-effort cancellation of the in-progress invocation for `task_id` +
  // `step_id`. A mutation already dispatched reports kOutcomeUnknown rather
  // than pretending it was stopped.
  virtual void Cancel(const TaskId &task_id, const std::string &step_id) {}
};

// Registry keyed by (capability id, version). Bounded; rejects duplicates.
class CapabilityExecutorRegistry {
public:
  CapabilityExecutorRegistry();
  CapabilityExecutorRegistry(const CapabilityExecutorRegistry &) = delete;
  CapabilityExecutorRegistry &
  operator=(const CapabilityExecutorRegistry &) = delete;
  ~CapabilityExecutorRegistry();

  // False on duplicate (id, version) or invalid id.
  bool Register(std::unique_ptr<CapabilityExecutor> executor);

  CapabilityExecutor *Find(const ToolId &id, int version) const;

  // Destroys executors immediately. Owners call this during orderly shutdown
  // while any external services observed by concrete executors are still
  // alive; relying on member destruction can cross keyed-service lifetimes.
  void Clear();

  // Every registered executor's (id, version).
  std::vector<std::pair<ToolId, int>> RegisteredCapabilities() const;

  // Completeness check both ways against a set of descriptors: descriptors
  // with no executor (those must be marked unavailable by the caller) and
  // executors with no descriptor (an architecture violation).
  struct CompletenessReport {
    CompletenessReport();
    CompletenessReport(const CompletenessReport &);
    CompletenessReport(CompletenessReport &&);
    CompletenessReport &operator=(const CompletenessReport &);
    CompletenessReport &operator=(CompletenessReport &&);
    ~CompletenessReport();

    std::vector<ToolId> descriptors_without_executor;
    std::vector<ToolId> executors_without_descriptor;
  };
  CompletenessReport
  CheckCompleteness(const std::vector<ToolDescriptor> &descriptors) const;

  size_t size() const { return executors_.size(); }

private:
  std::map<std::pair<std::string, int>, std::unique_ptr<CapabilityExecutor>>
      executors_;
};

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_CAPABILITY_EXECUTOR_H_
