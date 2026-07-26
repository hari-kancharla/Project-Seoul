// Project Seoul product runtime: the general planner.
// Turns an arbitrary user goal plus the capability graph into a validated
// typed plan. Two paths, both generic:
//
//  - Model-backed: the goal and the available capability contracts (ids,
//    descriptions, input schemas) are sent to the selected reasoning
//    provider, which returns structured plan JSON. The output is untrusted:
//    it is parsed with the bounded codec and must pass ValidatePlan against
//    the registry and permission context before anything runs.
//  - Deterministic fallback: a single-capability plan chosen by lexical
//    relevance between the goal and each descriptor's own name, description,
//    and tags. The scoring is data-driven from the registry; there is no
//    keyword-to-workflow table, no domain switch, and no phrase-specific
//    handler anywhere in this file.
//
// Approval gates are injected from descriptor policy (risk + approval), never
// from plan text.

#ifndef SEOUL_BROWSER_PRODUCT_PLANNER_H_
#define SEOUL_BROWSER_PRODUCT_PLANNER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "seoul/browser/tasks/plan_types.h"
#include "seoul/browser/tools/tool_registry.h"
#include "seoul/browser/tools/tool_types.h"

namespace seoul {

// How the plan was produced; recorded on the task for transparency.
enum class PlanOrigin {
  kDeterministic,
  kLocalModel,
  kCloudModel,
};

struct PlannerResult {
  PlannerResult();
  PlannerResult(PlannerResult&&);
  PlannerResult& operator=(PlannerResult&&);
  ~PlannerResult();

  bool ok = false;
  Plan plan;
  PlanOrigin origin = PlanOrigin::kDeterministic;
  std::string failure;  // human-readable reason when !ok
};

// Seam to the reasoning layer, injected by the runtime. Sends `prompt` to the
// route's provider and returns the raw structured output (or nullopt). The
// planner never talks to a provider type directly, so this target stays pure.
using ModelPlanRequester = base::RepeatingCallback<void(
    const std::string& prompt,
    bool prefer_local,
    bool allow_cloud_models,
    base::OnceCallback<void(std::optional<base::DictValue> output,
                            PlanOrigin origin)>)>;

class Planner {
 public:
  // `registry` must outlive the planner (the runtime owns both).
  Planner(const ToolRegistry& registry, ModelPlanRequester model_requester);
  Planner(const Planner&) = delete;
  Planner& operator=(const Planner&) = delete;
  ~Planner();

  // Builds a plan for `goal` over the capabilities available under `context`.
  // Tries the model path when `use_model` and the requester is set; falls
  // back to the deterministic path on any model failure. The callback runs
  // exactly once on the calling sequence.
  void BuildPlan(const std::string& goal,
                 const ToolPermissionContext& context,
                 bool use_model,
                 bool prefer_local,
                 bool allow_cloud_models,
                 base::OnceCallback<void(PlannerResult)> callback);

  // Compatibility overload for callers that do not impose a Scene model
  // policy. Runtime tasks use the explicit overload above.
  void BuildPlan(const std::string& goal,
                 const ToolPermissionContext& context,
                 bool use_model,
                 bool prefer_local,
                 base::OnceCallback<void(PlannerResult)> callback);

  // The deterministic path, exposed for tests and for replanning under a
  // degraded provider. Pure and synchronous.
  PlannerResult BuildDeterministic(const std::string& goal,
                                   const ToolPermissionContext& context) const;

  // The exact prompt sent to a reasoning provider for `goal`; exposed so tests
  // can assert capability contracts (not prose heuristics) drive planning.
  static std::string BuildPlanningPrompt(
      const std::string& goal,
      const std::vector<const ToolDescriptor*>& capabilities);

  // Injects approval gates required by descriptor policy that the plan is
  // missing, so a model can never plan around an approval requirement.
  static void EnforceApprovalPolicy(Plan& plan, const ToolRegistry& registry);

 private:
  void OnModelOutput(const std::string& goal,
                     ToolPermissionContext context,
                     base::OnceCallback<void(PlannerResult)> callback,
                     std::optional<base::DictValue> output,
                     PlanOrigin origin);

  const raw_ref<const ToolRegistry> registry_;
  ModelPlanRequester model_requester_;
  base::WeakPtrFactory<Planner> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_PLANNER_H_
