// Project Seoul workflow system.

#include "seoul/browser/workflows/workflow_graph.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace seoul {

// Clone-based copy semantics for the value-holding workflow structs
// (base::Value/Dict are move-only). Move and destruction are member-wise.
WorkflowNode::WorkflowNode() = default;
WorkflowNode::WorkflowNode(WorkflowNode &&) = default;
WorkflowNode &WorkflowNode::operator=(WorkflowNode &&) = default;
WorkflowNode::~WorkflowNode() = default;
WorkflowNode::WorkflowNode(const WorkflowNode &other)
    : id(other.id), kind(other.kind), label(other.label), tool(other.tool),
      args(other.args.Clone()), prompt(other.prompt),
      requires_approval(other.requires_approval),
      max_iterations(other.max_iterations) {}
WorkflowNode &WorkflowNode::operator=(const WorkflowNode &other) {
  id = other.id;
  kind = other.kind;
  label = other.label;
  tool = other.tool;
  args = other.args.Clone();
  prompt = other.prompt;
  requires_approval = other.requires_approval;
  max_iterations = other.max_iterations;
  return *this;
}

WorkflowParam::WorkflowParam() = default;
WorkflowParam::WorkflowParam(WorkflowParam &&) = default;
WorkflowParam &WorkflowParam::operator=(WorkflowParam &&) = default;
WorkflowParam::~WorkflowParam() = default;
WorkflowParam::WorkflowParam(const WorkflowParam &other)
    : field(other.field), default_value(other.default_value.Clone()) {}
WorkflowParam &WorkflowParam::operator=(const WorkflowParam &other) {
  field = other.field;
  default_value = other.default_value.Clone();
  return *this;
}

namespace {

bool ValidNodeId(const std::string &id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  if (id[0] < 'a' || id[0] > 'z') {
    return false;
  }
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-')) {
      return false;
    }
  }
  return true;
}

const WorkflowNode *FindNode(const WorkflowDefinition &definition,
                             const std::string &id) {
  for (const WorkflowNode &node : definition.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

// Collects "{{param:<name>}}" references from a string.
bool ExtractParamReference(const std::string &text, std::string *name) {
  constexpr std::string_view kPrefix = "{{param:";
  constexpr std::string_view kSuffix = "}}";
  if (text.size() <= kPrefix.size() + kSuffix.size()) {
    return false;
  }
  if (text.compare(0, kPrefix.size(), kPrefix) != 0 ||
      text.compare(text.size() - kSuffix.size(), kSuffix.size(), kSuffix) !=
          0) {
    return false;
  }
  *name = text.substr(kPrefix.size(),
                      text.size() - kPrefix.size() - kSuffix.size());
  return !name->empty();
}

WorkflowStatusResult
ValidateParamReferences(const WorkflowDefinition &definition,
                        const base::DictValue &args) {
  for (const auto [key, value] : args) {
    if (value.is_string()) {
      std::string param_name;
      if (ExtractParamReference(value.GetString(), &param_name)) {
        bool declared = false;
        for (const WorkflowParam &param : definition.params) {
          if (param.field.name == param_name) {
            declared = true;
            break;
          }
        }
        if (!declared) {
          return base::unexpected(WorkflowError::kUnknownParamReference);
        }
      }
    } else if (value.is_dict()) {
      if (auto result = ValidateParamReferences(definition, value.GetDict());
          !result.has_value()) {
        return result;
      }
    }
  }
  return base::ok();
}

WorkflowStatusResult ValidateTrigger(const WorkflowTrigger &trigger) {
  switch (trigger.kind) {
  case WorkflowTriggerKind::kManual:
  case WorkflowTriggerKind::kStartup:
    return base::ok();
  case WorkflowTriggerKind::kSchedule:
    if (trigger.interval_minutes < kMinTriggerIntervalMinutes ||
        trigger.interval_minutes > kMaxTriggerIntervalMinutes) {
      return base::unexpected(WorkflowError::kInvalidTrigger);
    }
    return base::ok();
  case WorkflowTriggerKind::kSceneActivation:
    if (trigger.scene_id.empty()) {
      return base::unexpected(WorkflowError::kInvalidTrigger);
    }
    return base::ok();
  case WorkflowTriggerKind::kNavigation:
  case WorkflowTriggerKind::kPageStateChange:
    if (trigger.origin_pattern.empty() ||
        trigger.origin_pattern.size() > 1024) {
      return base::unexpected(WorkflowError::kInvalidTrigger);
    }
    return base::ok();
  case WorkflowTriggerKind::kServiceEvent:
    if (trigger.event_name.empty()) {
      return base::unexpected(WorkflowError::kInvalidTrigger);
    }
    return base::ok();
  }
  return base::unexpected(WorkflowError::kInvalidTrigger);
}

// Substitutes param placeholders in one args dictionary with typed values.
WorkflowResult<base::DictValue>
SubstituteParams(const WorkflowDefinition &definition,
                 const base::DictValue &args,
                 const base::DictValue &param_values) {
  base::DictValue resolved;
  for (const auto [key, value] : args) {
    if (value.is_string()) {
      std::string param_name;
      if (ExtractParamReference(value.GetString(), &param_name)) {
        const base::Value *run_value = param_values.Find(param_name);
        if (!run_value) {
          for (const WorkflowParam &param : definition.params) {
            if (param.field.name == param_name &&
                !param.default_value.is_none()) {
              run_value = &param.default_value;
              break;
            }
          }
        }
        if (!run_value) {
          return base::unexpected(WorkflowError::kUnknownParamReference);
        }
        resolved.Set(key, run_value->Clone());
        continue;
      }
      resolved.Set(key, value.Clone());
      continue;
    }
    if (value.is_dict()) {
      auto nested = SubstituteParams(definition, value.GetDict(), param_values);
      if (!nested.has_value()) {
        return base::unexpected(nested.error());
      }
      resolved.Set(key, std::move(nested.value()));
      continue;
    }
    resolved.Set(key, value.Clone());
  }
  return resolved;
}

} // namespace

WorkflowStatusResult
ValidateWorkflowStructure(const WorkflowDefinition &definition) {
  if (definition.name.empty() ||
      definition.name.size() > kMaxWorkflowNameLength ||
      definition.description.size() > kMaxWorkflowDescriptionLength) {
    return base::unexpected(WorkflowError::kInvalidName);
  }
  if (definition.nodes.empty()) {
    return base::unexpected(WorkflowError::kEmptyWorkflow);
  }
  if (definition.nodes.size() > kMaxWorkflowNodes) {
    return base::unexpected(WorkflowError::kTooManyNodes);
  }
  if (definition.edges.size() > kMaxWorkflowEdges) {
    return base::unexpected(WorkflowError::kTooManyEdges);
  }
  if (definition.params.size() > kMaxWorkflowParams) {
    return base::unexpected(WorkflowError::kTooManyParams);
  }
  for (const WorkflowParam &param : definition.params) {
    ToolSchema single;
    single.fields.push_back(param.field);
    if (!IsWellFormedSchema(single)) {
      return base::unexpected(WorkflowError::kInvalidParam);
    }
  }

  std::set<std::string> node_ids;
  for (const WorkflowNode &node : definition.nodes) {
    if (!ValidNodeId(node.id)) {
      return base::unexpected(WorkflowError::kInvalidNodeId);
    }
    if (!node_ids.insert(node.id).second) {
      return base::unexpected(WorkflowError::kDuplicateNodeId);
    }
    if ((node.kind == WorkflowNodeKind::kApproval ||
         node.kind == WorkflowNodeKind::kUserInput) &&
        node.prompt.empty()) {
      return base::unexpected(WorkflowError::kMissingPrompt);
    }
    if (node.max_iterations < 0 ||
        node.max_iterations > kMaxWorkflowLoopIterations) {
      return base::unexpected(WorkflowError::kLoopTooLarge);
    }
    if (node.kind == WorkflowNodeKind::kToolStep) {
      if (auto refs = ValidateParamReferences(definition, node.args);
          !refs.has_value()) {
        return refs;
      }
    }
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const WorkflowEdge &edge : definition.edges) {
    if (node_ids.find(edge.from) == node_ids.end() ||
        node_ids.find(edge.to) == node_ids.end()) {
      return base::unexpected(WorkflowError::kEdgeUnknownNode);
    }
    if (edge.from == edge.to) {
      return base::unexpected(WorkflowError::kSelfEdge);
    }
    if (!seen_edges.insert({edge.from, edge.to}).second) {
      return base::unexpected(WorkflowError::kDuplicateEdge);
    }
    if (edge.kind == WorkflowEdgeKind::kLoopBack) {
      const WorkflowNode *header = FindNode(definition, edge.to);
      if (!header || header->max_iterations == 0) {
        return base::unexpected(header ? WorkflowError::kLoopUnbounded
                                       : WorkflowError::kEdgeUnknownNode);
      }
    }
  }

  // Every loop header must be the target of a loop-back edge; otherwise its
  // bound is dead configuration.
  for (const WorkflowNode &node : definition.nodes) {
    if (node.max_iterations > 0) {
      bool targeted = false;
      for (const WorkflowEdge &edge : definition.edges) {
        if (edge.kind == WorkflowEdgeKind::kLoopBack && edge.to == node.id) {
          targeted = true;
          break;
        }
      }
      if (!targeted) {
        return base::unexpected(WorkflowError::kLoopHeaderUnreachable);
      }
    }
  }

  if (auto trigger = ValidateTrigger(definition.trigger);
      !trigger.has_value()) {
    return trigger;
  }

  // Cycle check over non-loop-back edges.
  if (auto order = TopologicalOrder(definition); !order.has_value()) {
    return base::unexpected(order.error());
  }
  return base::ok();
}

WorkflowResult<std::vector<std::string>>
TopologicalOrder(const WorkflowDefinition &definition) {
  std::map<std::string, int> incoming;
  std::map<std::string, std::vector<std::string>> outgoing;
  for (const WorkflowNode &node : definition.nodes) {
    incoming[node.id] = 0;
  }
  for (const WorkflowEdge &edge : definition.edges) {
    if (edge.kind == WorkflowEdgeKind::kLoopBack) {
      continue;
    }
    if (incoming.find(edge.from) == incoming.end() ||
        incoming.find(edge.to) == incoming.end()) {
      return base::unexpected(WorkflowError::kEdgeUnknownNode);
    }
    outgoing[edge.from].push_back(edge.to);
    ++incoming[edge.to];
  }
  std::vector<std::string> ready;
  for (const auto &[id, count] : incoming) {
    if (count == 0) {
      ready.push_back(id);
    }
  }
  std::vector<std::string> order;
  while (!ready.empty()) {
    std::sort(ready.begin(), ready.end());
    const std::string current = ready.front();
    ready.erase(ready.begin());
    order.push_back(current);
    for (const std::string &next : outgoing[current]) {
      if (--incoming[next] == 0) {
        ready.push_back(next);
      }
    }
  }
  if (order.size() != definition.nodes.size()) {
    return base::unexpected(WorkflowError::kCycleWithoutLoop);
  }
  return order;
}

WorkflowResult<Plan> CompileWorkflow(const WorkflowDefinition &definition,
                                     const base::DictValue &param_values,
                                     const TaskBudgets &budgets) {
  if (auto valid = ValidateWorkflowStructure(definition); !valid.has_value()) {
    return base::unexpected(valid.error());
  }
  auto order = TopologicalOrder(definition);
  if (!order.has_value()) {
    return base::unexpected(order.error());
  }

  // Loop groups: a loop-back edge from S to header H covers every node whose
  // topological position lies within [H, S].
  std::map<std::string, size_t> position;
  for (size_t i = 0; i < order.value().size(); ++i) {
    position[order.value()[i]] = i;
  }
  std::map<std::string, int> loop_group_of;
  std::map<std::string, int> loop_bound_of;
  int next_loop_group = 1;
  for (const WorkflowEdge &edge : definition.edges) {
    if (edge.kind != WorkflowEdgeKind::kLoopBack) {
      continue;
    }
    const size_t start = position[edge.to];
    const size_t end = position[edge.from];
    if (start > end) {
      return base::unexpected(WorkflowError::kLoopHeaderUnreachable);
    }
    const WorkflowNode *header = FindNode(definition, edge.to);
    const int group = next_loop_group++;
    for (size_t i = start; i <= end; ++i) {
      loop_group_of[order.value()[i]] = group;
      loop_bound_of[order.value()[i]] = header->max_iterations;
    }
  }

  Plan plan;
  plan.goal = definition.name;
  plan.budgets = budgets;
  for (const std::string &node_id : order.value()) {
    const WorkflowNode *node = FindNode(definition, node_id);
    PlanStep step;
    step.id = node->id;
    switch (node->kind) {
    case WorkflowNodeKind::kToolStep: {
      step.kind = PlanStepKind::kToolCall;
      step.tool = node->tool;
      auto args = SubstituteParams(definition, node->args, param_values);
      if (!args.has_value()) {
        return base::unexpected(args.error());
      }
      step.args = std::move(args.value());
      step.requires_approval = node->requires_approval;
      break;
    }
    case WorkflowNodeKind::kApproval:
      step.kind = PlanStepKind::kApprovalGate;
      step.prompt = node->prompt;
      break;
    case WorkflowNodeKind::kUserInput:
      step.kind = PlanStepKind::kUserInput;
      step.prompt = node->prompt;
      break;
    }
    // Outcome-conditional edges become guards; a node with several incoming
    // conditional edges is guarded by the first in edge order
    // (deterministic; richer join semantics are a schema bump).
    for (const WorkflowEdge &edge : definition.edges) {
      if (edge.to != node->id) {
        continue;
      }
      if (edge.kind == WorkflowEdgeKind::kOnSuccess ||
          edge.kind == WorkflowEdgeKind::kOnFailure) {
        StepGuard guard;
        guard.depends_on_step = edge.from;
        guard.require_success = edge.kind == WorkflowEdgeKind::kOnSuccess;
        step.guard = guard;
        break;
      }
    }
    auto loop_it = loop_group_of.find(node->id);
    if (loop_it != loop_group_of.end()) {
      step.loop_group = loop_it->second;
      step.max_iterations = loop_bound_of[node->id];
    }
    plan.steps.push_back(std::move(step));
  }
  return plan;
}

const char *WorkflowErrorToString(WorkflowError error) {
  switch (error) {
  case WorkflowError::kInvalidName:
    return "invalid_name";
  case WorkflowError::kEmptyWorkflow:
    return "empty_workflow";
  case WorkflowError::kTooManyNodes:
    return "too_many_nodes";
  case WorkflowError::kTooManyEdges:
    return "too_many_edges";
  case WorkflowError::kTooManyParams:
    return "too_many_params";
  case WorkflowError::kInvalidNodeId:
    return "invalid_node_id";
  case WorkflowError::kDuplicateNodeId:
    return "duplicate_node_id";
  case WorkflowError::kUnknownNode:
    return "unknown_node";
  case WorkflowError::kMissingPrompt:
    return "missing_prompt";
  case WorkflowError::kEdgeUnknownNode:
    return "edge_unknown_node";
  case WorkflowError::kSelfEdge:
    return "self_edge";
  case WorkflowError::kDuplicateEdge:
    return "duplicate_edge";
  case WorkflowError::kCycleWithoutLoop:
    return "cycle_without_loop";
  case WorkflowError::kLoopBackNotToHeader:
    return "loop_back_not_to_header";
  case WorkflowError::kLoopUnbounded:
    return "loop_unbounded";
  case WorkflowError::kLoopTooLarge:
    return "loop_too_large";
  case WorkflowError::kLoopHeaderUnreachable:
    return "loop_header_unreachable";
  case WorkflowError::kInvalidTrigger:
    return "invalid_trigger";
  case WorkflowError::kUnknownParamReference:
    return "unknown_param_reference";
  case WorkflowError::kInvalidParam:
    return "invalid_param";
  case WorkflowError::kUnsupportedSchema:
    return "unsupported_schema";
  case WorkflowError::kUnknownTool:
    return "unknown_tool";
  case WorkflowError::kArgsInvalid:
    return "args_invalid";
  case WorkflowError::kUnknownWorkflow:
    return "unknown_workflow";
  case WorkflowError::kLimitExceeded:
    return "limit_exceeded";
  case WorkflowError::kInUse:
    return "in_use";
  }
  return "invalid_name";
}

} // namespace seoul
