// Project Seoul product runtime: the general planner.

#include "seoul/browser/product/planner.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "seoul/browser/product/plan_json.h"
#include "seoul/browser/tasks/plan_validator.h"
#include "seoul/browser/tools/tool_schema.h"

namespace seoul {

namespace {

bool IsStopWord(std::string_view token) {
  // These function words carry no capability intent. Leaving them in lets a
  // verbose description win because it says "in the current window", even
  // when the goal's explicit operation is "list tabs".
  static constexpr std::string_view kStopWords[] = {
      "an", "and", "as", "at", "be", "by",   "for", "from", "in", "into",
      "is", "it",  "of", "on", "or", "that", "the", "this", "to", "with",
  };
  return std::ranges::find(kStopWords, token) != std::end(kStopWords);
}

// Lowercased word tokens of length >= 2. Generic lexical tokenization; the
// vocabulary comes entirely from the goal and the registered descriptors.
std::vector<std::string> Tokenize(std::string_view text) {
  std::vector<std::string> tokens;
  std::string current;
  for (const char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      current.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else if (!current.empty()) {
      if (current.size() >= 2 && !IsStopWord(current)) {
        tokens.push_back(current);
      }
      current.clear();
    }
  }
  if (current.size() >= 2 && !IsStopWord(current)) {
    tokens.push_back(current);
  }
  return tokens;
}

// Data-driven relevance: token overlap between the goal and this descriptor's
// own text (id segments, name, description, tags). No fixed vocabulary.
double ScoreCapability(const std::set<std::string>& goal_tokens,
                       const ToolDescriptor& descriptor) {
  std::vector<std::string> descriptor_tokens = Tokenize(descriptor.id.value());
  const std::vector<std::string> name_tokens = Tokenize(descriptor.name);
  const std::vector<std::string> description_tokens =
      Tokenize(descriptor.description);
  descriptor_tokens.insert(descriptor_tokens.end(), name_tokens.begin(),
                           name_tokens.end());
  descriptor_tokens.insert(descriptor_tokens.end(), description_tokens.begin(),
                           description_tokens.end());
  for (const std::string& tag : descriptor.capability_tags) {
    const std::vector<std::string> tag_tokens = Tokenize(tag);
    descriptor_tokens.insert(descriptor_tokens.end(), tag_tokens.begin(),
                             tag_tokens.end());
  }
  if (descriptor_tokens.empty()) {
    return 0.0;
  }
  double matches = 0.0;
  std::set<std::string> seen;
  for (const std::string& token : descriptor_tokens) {
    if (goal_tokens.count(token) && seen.insert(token).second) {
      matches += 1.0;
    }
  }
  // Normalize by goal size so verbose descriptors do not win by volume.
  return goal_tokens.empty() ? 0.0 : matches / goal_tokens.size();
}

// Extracts the first http(s) URL token from the goal, if any. Used only to
// fill a required url-shaped field; this is schema-driven, not
// capability-specific.
std::string FirstUrlInGoal(const std::string& goal) {
  const std::vector<std::string> pieces = base::SplitString(
      goal, " \t\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const std::string& piece : pieces) {
    if (piece.starts_with("http://") || piece.starts_with("https://")) {
      return piece;
    }
  }
  return std::string();
}

// Fills required top-level fields of `schema` generically by field kind: a
// url field takes a URL found in the goal; a required string field takes the
// goal text itself (the capability's own contract decides how to interpret
// it). Other required kinds are left unset and surface as validation
// failures rather than fabricated values.
base::DictValue SynthesizeArgs(const ToolSchema& schema,
                               const std::string& goal) {
  base::DictValue args;
  for (const SchemaField& field : schema.fields) {
    if (!field.required) {
      continue;
    }
    if (field.kind == SchemaFieldKind::kUrl) {
      const std::string url = FirstUrlInGoal(goal);
      if (!url.empty()) {
        args.Set(field.name, url);
      }
      continue;
    }
    if (field.kind == SchemaFieldKind::kString) {
      std::string text = goal;
      if (text.size() > field.max_length) {
        text.resize(field.max_length);
      }
      args.Set(field.name, text);
    }
  }
  return args;
}

const char* SchemaFieldKindName(SchemaFieldKind kind) {
  switch (kind) {
    case SchemaFieldKind::kString:
      return "string";
    case SchemaFieldKind::kInteger:
      return "integer";
    case SchemaFieldKind::kNumber:
      return "number";
    case SchemaFieldKind::kBoolean:
      return "boolean";
    case SchemaFieldKind::kEnum:
      return "enum";
    case SchemaFieldKind::kUrl:
      return "url";
    case SchemaFieldKind::kList:
      return "list";
    case SchemaFieldKind::kObject:
      return "object";
  }
  return "string";
}

}  // namespace

PlannerResult::PlannerResult() = default;
PlannerResult::PlannerResult(PlannerResult&&) = default;
PlannerResult& PlannerResult::operator=(PlannerResult&&) = default;
PlannerResult::~PlannerResult() = default;

Planner::Planner(const ToolRegistry& registry,
                 ModelPlanRequester model_requester)
    : registry_(registry), model_requester_(std::move(model_requester)) {}

Planner::~Planner() = default;

// static
std::string Planner::BuildPlanningPrompt(
    const std::string& goal,
    const std::vector<const ToolDescriptor*>& capabilities) {
  base::DictValue request;
  request.Set("instruction",
              "Produce a plan as JSON with fields goal and steps. Each step "
              "is {id, kind, tool, args} with kind tool_call, or {id, kind, "
              "prompt} with kind approval_gate or user_input. Use only the "
              "capabilities listed. Do not invent capabilities. Do not emit "
              "code.");
  request.Set("goal", goal);
  base::ListValue capability_list;
  for (const ToolDescriptor* descriptor : capabilities) {
    base::DictValue entry;
    entry.Set("id", descriptor->id.value());
    entry.Set("description", descriptor->description);
    base::DictValue input;
    for (const SchemaField& field : descriptor->input_schema.fields) {
      base::DictValue field_spec;
      field_spec.Set("type", SchemaFieldKindName(field.kind));
      field_spec.Set("required", field.required);
      if (!field.description.empty()) {
        field_spec.Set("description", field.description);
      }
      input.Set(field.name, std::move(field_spec));
    }
    entry.Set("input_schema", std::move(input));
    capability_list.Append(std::move(entry));
  }
  request.Set("capabilities", std::move(capability_list));
  std::string prompt;
  base::JSONWriter::Write(request, &prompt);
  return prompt;
}

// static
void Planner::EnforceApprovalPolicy(Plan& plan, const ToolRegistry& registry) {
  for (PlanStep& step : plan.steps) {
    if (step.kind != PlanStepKind::kToolCall) {
      continue;
    }
    const ToolDescriptor* descriptor = registry.Find(step.tool);
    if (!descriptor) {
      continue;  // ValidatePlan rejects the unknown tool
    }
    if (descriptor->approval == ApprovalPolicy::kAlwaysRequired ||
        descriptor->approval == ApprovalPolicy::kFirstUsePerScope ||
        descriptor->risk == RiskCategory::kIrreversibleMutation ||
        descriptor->risk == RiskCategory::kExternalSideEffect) {
      step.requires_approval = true;
    }
  }
}

PlannerResult Planner::BuildDeterministic(
    const std::string& goal,
    const ToolPermissionContext& context) const {
  PlannerResult result;
  result.origin = PlanOrigin::kDeterministic;
  if (goal.empty()) {
    result.failure = "Empty goal.";
    return result;
  }
  const std::vector<const ToolDescriptor*> capabilities =
      registry_->ListAvailable(context);
  const std::vector<std::string> goal_token_list = Tokenize(goal);
  const std::set<std::string> goal_tokens(goal_token_list.begin(),
                                          goal_token_list.end());
  const ToolDescriptor* best = nullptr;
  base::DictValue best_args;
  double best_score = 0.0;
  bool had_relevant_but_unsatisfied_candidate = false;
  for (const ToolDescriptor* descriptor : capabilities) {
    const double score = ScoreCapability(goal_tokens, *descriptor);
    if (score <= 0.0) {
      continue;
    }
    base::DictValue args = SynthesizeArgs(descriptor->input_schema, goal);
    // A high lexical match is not actionable when its required arguments
    // cannot be obtained from the goal. Keep looking for the next valid
    // capability instead of rejecting the entire deterministic plan.
    if (!ValidateArgs(descriptor->input_schema, args).has_value()) {
      had_relevant_but_unsatisfied_candidate = true;
      continue;
    }
    if (score > best_score) {
      best_score = score;
      best = descriptor;
      best_args = std::move(args);
    }
  }
  if (!best || best_score <= 0.0) {
    result.failure = had_relevant_but_unsatisfied_candidate
                         ? "A relevant capability was found, but the request "
                           "did not include its required arguments."
                         : "No available capability matches the request; "
                           "connect a capability that covers it.";
    return result;
  }

  Plan plan;
  plan.goal = goal;
  PlanStep step;
  step.id = "step_1";
  step.kind = PlanStepKind::kToolCall;
  step.tool = best->id;
  step.args = std::move(best_args);
  plan.steps.push_back(std::move(step));
  EnforceApprovalPolicy(plan, registry_.get());

  const PlanValidationResult validation =
      ValidatePlan(plan, registry_.get(), context);
  if (!validation.has_value()) {
    result.failure = std::string("Deterministic plan rejected: ") +
                     PlanErrorToString(validation.error().error);
    return result;
  }
  result.ok = true;
  result.plan = std::move(plan);
  return result;
}

void Planner::BuildPlan(const std::string& goal,
                        const ToolPermissionContext& context,
                        bool use_model,
                        bool prefer_local,
                        bool allow_cloud_models,
                        base::OnceCallback<void(PlannerResult)> callback) {
  if (use_model && model_requester_) {
    model_requester_.Run(
        BuildPlanningPrompt(goal, registry_->ListAvailable(context)),
        prefer_local, allow_cloud_models,
        base::BindOnce(&Planner::OnModelOutput, weak_factory_.GetWeakPtr(),
                       goal, context, std::move(callback)));
    return;
  }
  std::move(callback).Run(BuildDeterministic(goal, context));
}

void Planner::BuildPlan(const std::string& goal,
                        const ToolPermissionContext& context,
                        bool use_model,
                        bool prefer_local,
                        base::OnceCallback<void(PlannerResult)> callback) {
  BuildPlan(goal, context, use_model, prefer_local,
            /*allow_cloud_models=*/true, std::move(callback));
}

void Planner::OnModelOutput(const std::string& goal,
                            ToolPermissionContext context,
                            base::OnceCallback<void(PlannerResult)> callback,
                            std::optional<base::DictValue> output,
                            PlanOrigin origin) {
  if (output.has_value()) {
    std::optional<Plan> plan = PlanFromValue(output.value());
    if (plan.has_value()) {
      plan->goal = goal;  // display text is the user's own statement
      EnforceApprovalPolicy(plan.value(), registry_.get());
      const PlanValidationResult validation =
          ValidatePlan(plan.value(), registry_.get(), context);
      if (validation.has_value()) {
        PlannerResult result;
        result.ok = true;
        result.plan = std::move(plan.value());
        result.origin = origin;
        std::move(callback).Run(std::move(result));
        return;
      }
    }
  }
  // Any model failure falls back to the deterministic path; the origin then
  // truthfully reports deterministic.
  std::move(callback).Run(BuildDeterministic(goal, context));
}

}  // namespace seoul
