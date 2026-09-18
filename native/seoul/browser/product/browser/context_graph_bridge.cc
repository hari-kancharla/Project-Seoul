// Project Seoul: live metadata projected into the context graph.

#include "seoul/browser/product/browser/seoul_runtime_service.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "seoul/browser/context_map/context_map_builder.h"
#include "seoul/browser/library/library_service.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/product/thread_service.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/shell_service.h"

namespace seoul {

base::DictValue SeoulRuntimeService::ContextGraphSnapshot(
    const LiveWindowKey& window) {
  using namespace context_map;
  GraphInputs inputs;
  inputs.scope = GraphScope::kAllWindows;
  auto* live = live_window_state_provider();
  if (shutting_down_ || !organization_ || !live ||
      !live->GetSnapshot(window).has_value()) return base::DictValue();
  const auto organization = StudioOrganizationSnapshot();
  std::map<std::string, const TabMembershipRecord*> memberships;
  std::map<std::string, std::string> live_memberships;
  for (const auto& membership : organization.memberships)
    memberships[membership.tab_key] = &membership;
  for (const auto& workspace : organization.workspaces) {
    if (workspace.archived) continue;
    WorkspaceInput input;
    input.id = workspace.id.value();
    input.name = workspace.name;
    inputs.workspaces.push_back(std::move(input));
  }
  for (const auto& folder : organization.folders) {
    FolderInput input;
    input.id = folder.id.value();
    input.name = folder.name;
    input.workspace_id = folder.workspace_id.value();
    inputs.folders.push_back(std::move(input));
  }
  size_t window_number = 0;
  for (const auto& key : live->Windows()) {
    const auto snapshot = live->GetSnapshot(key);
    if (!snapshot || !snapshot->eligible) continue;
    WindowInput input;
    input.id = key.value();
    input.title = key == window ? "This window" :
        "Window " + base::NumberToString(++window_number);
    for (const auto& state : organization.window_states)
      if (state.window_key == key.value())
        input.workspace_id = state.active_workspace_id.value();
    inputs.windows.push_back(std::move(input));
    for (const auto& tab : snapshot->tabs) {
      const auto found = memberships.find(tab.tab.value());
      if (found == memberships.end()) continue;
      const auto& member = *found->second;
      TabInput item;
      item.id = member.id.value();
      item.title = member.custom_title.empty() ? tab.title : member.custom_title;
      item.origin = tab.origin;
      item.window_id = key.value();
      item.workspace_id = member.workspace_id.value();
      item.folder_id = member.folder_id.value();
      item.active = tab.is_active;
      for (const auto& split : organization.splits)
        if (std::ranges::find(split.pane_tab_keys, tab.tab.value()) !=
            split.pane_tab_keys.end()) item.split_id = split.id.value();
      live_memberships[tab.tab.value()] = item.id;
      if (key == window && tab.is_active) inputs.focus_tab_id = item.id;
      inputs.tabs.push_back(std::move(item));
    }
  }
  for (const auto& summary : threads()->Summaries()) {
    if (summary.archived) continue;
    const auto* thread = threads()->FindThread(summary.id);
    if (!thread) continue;
    ThreadInput input;
    input.id = summary.id;
    input.name = summary.name;
    for (const auto& context : thread->items()) {
      ContextItemInput item;
      item.id = context.id;
      item.title = context.title;
      item.subkind = ContextItemKindToString(context.kind);
      if (context.kind == ContextItemKind::kTabReference) {
        const auto found = live_memberships.find(context.reference);
        item.reference_live = found != live_memberships.end();
        if (item.reference_live) item.references_id = found->second;
      } else if (context.kind == ContextItemKind::kTaskOutput) {
        item.references_kind = NodeKind::kTask;
        item.references_id = context.reference;
        item.reference_live = tasks()->Snapshot(
            TaskId::FromString(context.reference)).has_value();
      }
      input.items.push_back(std::move(item));
    }
    inputs.threads.push_back(std::move(input));
  }
  for (const auto& task : tasks()->Snapshots()) {
    TaskInput input;
    input.id = task.id.value();
    input.goal = task.goal;
    input.window_id = task.window.value();
    input.state = task.state == TaskState::kCompleted ? NodeState::kFinished :
        task.state == TaskState::kFailed || task.state == TaskState::kCancelled ?
        NodeState::kError : NodeState::kRunning;
    inputs.tasks.push_back(std::move(input));
  }
  for (const auto& id : library()->Boards()) {
    const auto* board = library()->FindBoard(id);
    if (!board || board->archived) continue;
    BoardInput input;
    input.id = id.value();
    input.name = board->name;
    // Board links to arbitrary URLs are not evidence of a link to a live tab.
    inputs.boards.push_back(std::move(input));
  }
  const auto graph = BuildContextMapGraph(inputs, ++context_graph_revision_);
  base::DictValue result;
  result.Set("schema_version", 1);
  result.Set("revision", base::NumberToString(graph.revision));
  result.Set("scope", GraphScopeToString(graph.scope));
  result.Set("root_id", inputs.focus_tab_id.empty() ? graph.root_id :
      "tab:" + inputs.focus_tab_id);
  result.Set("truncated", graph.truncated);
  result.Set("total_nodes", static_cast<int>(graph.total_nodes));
  result.Set("total_edges", static_cast<int>(graph.total_edges));
  base::ListValue nodes;
  for (const auto& node : graph.nodes) {
    base::DictValue item;
    item.Set("id", node.id);
    item.Set("kind", NodeKindToString(node.kind));
    item.Set("title", node.title);
    item.Set("subtitle", node.subtitle);
    item.Set("subkind", node.subkind);
    item.Set("state", NodeStateToString(node.state));
    nodes.Append(std::move(item));
  }
  base::ListValue edges;
  for (const auto& edge : graph.edges) {
    base::DictValue item;
    item.Set("id", edge.id);
    item.Set("source", edge.source);
    item.Set("target", edge.target);
    item.Set("type", EdgeTypeToString(edge.type));
    item.Set("provenance", EdgeProvenanceToString(edge.provenance));
    edges.Append(std::move(item));
  }
  result.Set("nodes", std::move(nodes));
  result.Set("edges", std::move(edges));
  return result;
}

bool SeoulRuntimeService::ActivateContextTab(const LiveWindowKey& window,
                                             const std::string& node_id) {
  if (shutting_down_ || !organization_ || !node_id.starts_with("tab:"))
    return false;
  const auto* member = organization_->model().FindMembership(
      TabMembershipId::FromString(node_id.substr(4)));
  auto* live = live_window_state_provider();
  auto* shell = organization_->shell_service();
  auto* controller = shell ? shell->GetController(window) : nullptr;
  if (!member || !live || !controller) return false;
  // Resolve the durable id afresh. A moved, closed or replaced tab must not
  // accidentally activate the old session tab that occupied its position.
  for (const auto& key : live->Windows()) {
    const auto snapshot = live->GetSnapshot(key);
    if (!snapshot || !snapshot->eligible) continue;
    for (const auto& tab : snapshot->tabs)
      if (tab.tab.value() == member->tab_key)
        return controller->ActivateLiveTab(key, tab.tab).has_value();
  }
  return false;
}

}  // namespace seoul
