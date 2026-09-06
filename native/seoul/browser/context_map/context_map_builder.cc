// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/context_map/context_map_builder.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

namespace seoul::context_map {

WorkspaceInput::WorkspaceInput() = default;
WorkspaceInput::WorkspaceInput(const WorkspaceInput&) = default;
WorkspaceInput& WorkspaceInput::operator=(const WorkspaceInput&) = default;
WorkspaceInput::WorkspaceInput(WorkspaceInput&&) = default;
WorkspaceInput& WorkspaceInput::operator=(WorkspaceInput&&) = default;
WorkspaceInput::~WorkspaceInput() = default;

WindowInput::WindowInput() = default;
WindowInput::WindowInput(const WindowInput&) = default;
WindowInput& WindowInput::operator=(const WindowInput&) = default;
WindowInput::WindowInput(WindowInput&&) = default;
WindowInput& WindowInput::operator=(WindowInput&&) = default;
WindowInput::~WindowInput() = default;

FolderInput::FolderInput() = default;
FolderInput::FolderInput(const FolderInput&) = default;
FolderInput& FolderInput::operator=(const FolderInput&) = default;
FolderInput::FolderInput(FolderInput&&) = default;
FolderInput& FolderInput::operator=(FolderInput&&) = default;
FolderInput::~FolderInput() = default;

TabInput::TabInput() = default;
TabInput::TabInput(const TabInput&) = default;
TabInput& TabInput::operator=(const TabInput&) = default;
TabInput::TabInput(TabInput&&) = default;
TabInput& TabInput::operator=(TabInput&&) = default;
TabInput::~TabInput() = default;

ContextItemInput::ContextItemInput() = default;
ContextItemInput::ContextItemInput(const ContextItemInput&) = default;
ContextItemInput& ContextItemInput::operator=(const ContextItemInput&) = default;
ContextItemInput::ContextItemInput(ContextItemInput&&) = default;
ContextItemInput& ContextItemInput::operator=(ContextItemInput&&) = default;
ContextItemInput::~ContextItemInput() = default;

ThreadInput::ThreadInput() = default;
ThreadInput::ThreadInput(const ThreadInput&) = default;
ThreadInput& ThreadInput::operator=(const ThreadInput&) = default;
ThreadInput::ThreadInput(ThreadInput&&) = default;
ThreadInput& ThreadInput::operator=(ThreadInput&&) = default;
ThreadInput::~ThreadInput() = default;

TaskInput::TaskInput() = default;
TaskInput::TaskInput(const TaskInput&) = default;
TaskInput& TaskInput::operator=(const TaskInput&) = default;
TaskInput::TaskInput(TaskInput&&) = default;
TaskInput& TaskInput::operator=(TaskInput&&) = default;
TaskInput::~TaskInput() = default;

BoardInput::BoardInput() = default;
BoardInput::BoardInput(const BoardInput&) = default;
BoardInput& BoardInput::operator=(const BoardInput&) = default;
BoardInput::BoardInput(BoardInput&&) = default;
BoardInput& BoardInput::operator=(BoardInput&&) = default;
BoardInput::~BoardInput() = default;

GraphInputs::GraphInputs() = default;
GraphInputs::GraphInputs(const GraphInputs&) = default;
GraphInputs& GraphInputs::operator=(const GraphInputs&) = default;
GraphInputs::GraphInputs(GraphInputs&&) = default;
GraphInputs& GraphInputs::operator=(GraphInputs&&) = default;
GraphInputs::~GraphInputs() = default;

namespace {

// Node ids are prefixed by kind so two services cannot collide on a bare id,
// and so an id is self-describing when it appears in a log or a test failure.
constexpr char kSessionId[] = "session";

std::string NodeId(NodeKind kind, std::string_view durable_id) {
  return base::StrCat({NodeKindToString(kind), ":", durable_id});
}

// Truncates on a UTF-8 character boundary. Cutting mid-sequence would put an
// invalid string on the wire, which the schema's maxLength would not catch.
std::string Clamp(const std::string& text, size_t limit) {
  if (text.size() <= limit) {
    return text;
  }
  std::string clamped = text.substr(0, limit);
  while (!clamped.empty() &&
         (static_cast<unsigned char>(clamped.back()) & 0xC0) == 0x80) {
    clamped.pop_back();
  }
  if (!clamped.empty()) {
    clamped.pop_back();
  }
  return clamped;
}

class GraphAssembler {
 public:
  void AddNode(NodeKind kind,
               std::string_view durable_id,
               const std::string& title,
               const std::string& subtitle,
               NodeState state,
               const std::string& subkind = std::string()) {
    if (durable_id.empty()) {
      return;
    }
    ContextMapNode node;
    node.id = NodeId(kind, durable_id);
    // A duplicate id is one object described twice, not two objects.
    if (!seen_nodes_.insert(node.id).second) {
      return;
    }
    node.kind = kind;
    node.subkind = Clamp(subkind, 64);
    node.title = Clamp(title, kMaxNodeTitleLength);
    node.subtitle = Clamp(subtitle, kMaxNodeSubtitleLength);
    node.state = state;
    nodes_.push_back(std::move(node));
  }

  void AddEdge(EdgeType type,
               const std::string& source,
               const std::string& target,
               EdgeProvenance provenance) {
    if (source.empty() || target.empty() || source == target) {
      return;
    }
    // Recorded now, resolved later: an edge is kept only if both endpoints
    // survive node selection, so truncation can never leave one dangling.
    pending_.push_back({type, source, target, provenance});
  }

  ContextMapGraph Finish(GraphScope scope,
                         const std::string& root_id,
                         uint64_t revision,
                         const std::set<std::string>& keep) {
    ContextMapGraph graph;
    graph.revision = revision;
    graph.scope = scope;
    graph.root_id = root_id;

    std::vector<ContextMapNode> selected;
    for (ContextMapNode& node : nodes_) {
      if (keep.empty() || keep.contains(node.id)) {
        selected.push_back(std::move(node));
      }
    }
    graph.total_nodes = selected.size();

    // Everything the SCOPE selected, captured before the render bound removes
    // any of it, so the edge total below counts what the scope contained rather
    // than what survived.
    std::set<std::string> in_scope;
    for (const ContextMapNode& node : selected) {
      in_scope.insert(node.id);
    }

    if (selected.size() > kMaxGraphNodes) {
      // Keep what answers the question first. The bands are the specification's
      // own: the scope's centre, then what is alive or running, then what is
      // directly related to the centre, then what the person authored, then the
      // rest. Stable, so order within a band is still the deterministic order
      // the services gave.
      std::set<std::string> adjacent;
      for (const PendingEdge& edge : pending_) {
        if (edge.source == root_id) {
          adjacent.insert(edge.target);
        } else if (edge.target == root_id) {
          adjacent.insert(edge.source);
        }
      }
      std::stable_sort(selected.begin(), selected.end(),
                       [&root_id, &adjacent](const ContextMapNode& a,
                                             const ContextMapNode& b) {
                         return Priority(a, root_id, adjacent) <
                                Priority(b, root_id, adjacent);
                       });
      selected.resize(kMaxGraphNodes);
      graph.truncated = true;
    }

    std::set<std::string> present;
    for (const ContextMapNode& node : selected) {
      present.insert(node.id);
    }
    graph.nodes = std::move(selected);

    // Counted against the scope's own node set: had this been counted after the
    // endpoint filter below, total_edges would equal the number emitted at
    // exactly the moment edges were being discarded, and the document would
    // claim a complete edge set while hundreds were missing.
    std::set<std::string> counted;
    size_t total_edges = 0;
    for (const PendingEdge& edge : pending_) {
      if (!in_scope.contains(edge.source) || !in_scope.contains(edge.target)) {
        continue;
      }
      if (counted.insert(base::StrCat({EdgeTypeToString(edge.type), "|",
                                       edge.source, "|", edge.target}))
              .second) {
        ++total_edges;
      }
    }

    std::set<std::string> seen_edges;
    for (const PendingEdge& edge : pending_) {
      if (!present.contains(edge.source) || !present.contains(edge.target)) {
        continue;
      }
      const std::string id =
          base::StrCat({EdgeTypeToString(edge.type), "|", edge.source, "|",
                        edge.target});
      if (!seen_edges.insert(id).second) {
        continue;
      }
      if (graph.edges.size() >= kMaxGraphEdges) {
        continue;
      }
      ContextMapEdge out;
      out.id = id;
      out.source = edge.source;
      out.target = edge.target;
      out.type = edge.type;
      out.provenance = edge.provenance;
      graph.edges.push_back(std::move(out));
    }
    graph.total_edges = total_edges;
    if (total_edges > graph.edges.size()) {
      graph.truncated = true;
    }
    return graph;
  }

  const std::vector<ContextMapNode>& nodes() const { return nodes_; }

  struct PendingEdge {
    EdgeType type;
    std::string source;
    std::string target;
    EdgeProvenance provenance;
  };
  const std::vector<PendingEdge>& pending() const { return pending_; }

 private:
  // Authored context outranks anything the browser merely happens to be
  // showing. A Project, its notes and a Board are what a person made; a live
  // tab is one of hundreds and costs nothing to lose from a picture. Ranking a
  // stale authored item below a finished task - as ranking on state alone does
  // - deletes exactly what the map promises to keep.
  static bool IsAuthored(NodeKind kind) {
    switch (kind) {
      case NodeKind::kThread:
      case NodeKind::kContextItem:
      case NodeKind::kBoard:
      case NodeKind::kArtifact:
        return true;
      default:
        return false;
    }
  }

  static int Priority(const ContextMapNode& node,
                      const std::string& root_id,
                      const std::set<std::string>& adjacent) {
    if (node.id == root_id) {
      return 0;
    }
    if (node.state == NodeState::kActive ||
        node.state == NodeState::kRunning ||
        node.state == NodeState::kError) {
      return 1;
    }
    // A centre rendered as an isolated dot answers nothing, so what touches it
    // survives before anything further out.
    if (adjacent.contains(node.id)) {
      return 2;
    }
    if (IsAuthored(node.kind)) {
      return 3;
    }
    return node.state == NodeState::kLive ? 4 : 5;
  }

  std::vector<ContextMapNode> nodes_;
  std::vector<PendingEdge> pending_;
  std::set<std::string> seen_nodes_;
};

// The set of nodes within `depth` hops of `root`, over the undirected form of
// the graph. Used only by Focus; every other scope shows what it gathered.
std::set<std::string> WithinDepth(
    const std::vector<GraphAssembler::PendingEdge>& edges,
    const std::string& root,
    size_t depth) {
  // The session node touches every window, so leaving it in the walk puts any
  // window in any Space three hops from any tab - which would make the deepest
  // Focus reach exactly the whole profile it is defined never to show.
  const std::string session = NodeId(NodeKind::kSession, kSessionId);
  std::map<std::string, std::vector<std::string>> adjacency;
  for (const auto& edge : edges) {
    if (edge.source == session || edge.target == session) {
      continue;
    }
    adjacency[edge.source].push_back(edge.target);
    adjacency[edge.target].push_back(edge.source);
  }
  std::set<std::string> reached{root};
  std::vector<std::string> frontier{root};
  for (size_t hop = 0; hop < depth && !frontier.empty(); ++hop) {
    std::vector<std::string> next;
    for (const std::string& node : frontier) {
      const auto it = adjacency.find(node);
      if (it == adjacency.end()) {
        continue;
      }
      for (const std::string& neighbour : it->second) {
        if (reached.insert(neighbour).second) {
          next.push_back(neighbour);
        }
      }
    }
    frontier = std::move(next);
  }
  return reached;
}

}  // namespace

ContextMapGraph BuildContextMapGraph(const GraphInputs& inputs,
                                     uint64_t revision) {
  GraphAssembler assembler;

  assembler.AddNode(NodeKind::kSession, kSessionId, "This session",
                    std::string(), NodeState::kLive);

  for (const WorkspaceInput& workspace : inputs.workspaces) {
    assembler.AddNode(NodeKind::kWorkspace, workspace.id, workspace.name,
                      std::string(), NodeState::kLive);
  }
  for (const WindowInput& window : inputs.windows) {
    assembler.AddNode(NodeKind::kWindow, window.id, window.title, std::string(),
                      NodeState::kLive);
    assembler.AddEdge(EdgeType::kContains, NodeId(NodeKind::kSession, kSessionId),
                      NodeId(NodeKind::kWindow, window.id),
                      EdgeProvenance::kSystem);
    if (!window.workspace_id.empty()) {
      assembler.AddEdge(EdgeType::kActiveIn,
                        NodeId(NodeKind::kWorkspace, window.workspace_id),
                        NodeId(NodeKind::kWindow, window.id),
                        EdgeProvenance::kSystem);
    }
  }
  for (const FolderInput& folder : inputs.folders) {
    assembler.AddNode(NodeKind::kFolder, folder.id, folder.name, std::string(),
                      NodeState::kLive);
    assembler.AddEdge(EdgeType::kBelongsTo, NodeId(NodeKind::kFolder, folder.id),
                      NodeId(NodeKind::kWorkspace, folder.workspace_id),
                      EdgeProvenance::kSystem);
  }

  // Splits are a node so the relationship is visible as a thing rather than as
  // a quietly implied pairing.
  std::map<std::string, std::vector<std::string>> split_members;
  for (const TabInput& tab : inputs.tabs) {
    assembler.AddNode(NodeKind::kTab, tab.id, tab.title, tab.origin,
                      tab.active ? NodeState::kActive : NodeState::kLive);
    const std::string tab_node = NodeId(NodeKind::kTab, tab.id);
    if (!tab.window_id.empty()) {
      assembler.AddEdge(EdgeType::kContains,
                        NodeId(NodeKind::kWindow, tab.window_id), tab_node,
                        EdgeProvenance::kSystem);
      if (tab.active) {
        assembler.AddEdge(EdgeType::kActiveIn, tab_node,
                          NodeId(NodeKind::kWindow, tab.window_id),
                          EdgeProvenance::kSystem);
      }
    }
    if (!tab.workspace_id.empty()) {
      assembler.AddEdge(EdgeType::kBelongsTo, tab_node,
                        NodeId(NodeKind::kWorkspace, tab.workspace_id),
                        EdgeProvenance::kSystem);
    }
    if (!tab.folder_id.empty()) {
      assembler.AddEdge(EdgeType::kGroupedIn, tab_node,
                        NodeId(NodeKind::kFolder, tab.folder_id),
                        EdgeProvenance::kSystem);
    }
    if (!tab.split_id.empty()) {
      split_members[tab.split_id].push_back(tab_node);
    }
  }
  for (const auto& [split_id, members] : split_members) {
    // A split needs two sides. One member is a stale id or a half-applied
    // snapshot, and drawing a "Split" joined to a single page reads as a
    // rendering fault in a graph whose whole claim is that every edge is
    // evidence.
    if (members.size() < 2u) {
      continue;
    }
    assembler.AddNode(NodeKind::kSplit, split_id, "Split", std::string(),
                      NodeState::kLive);
    for (const std::string& member : members) {
      assembler.AddEdge(EdgeType::kSplitWith,
                        NodeId(NodeKind::kSplit, split_id), member,
                        EdgeProvenance::kSystem);
    }
  }

  for (const ThreadInput& thread : inputs.threads) {
    assembler.AddNode(NodeKind::kThread, thread.id, thread.name, std::string(),
                      NodeState::kLive);
    const std::string thread_node = NodeId(NodeKind::kThread, thread.id);
    for (const ContextItemInput& item : thread.items) {
      assembler.AddNode(NodeKind::kContextItem, item.id, item.title,
                        std::string(),
                        item.reference_live ? NodeState::kLive
                                            : NodeState::kStale,
                        item.subkind);
      const std::string item_node = NodeId(NodeKind::kContextItem, item.id);
      assembler.AddEdge(EdgeType::kAttachedTo, item_node, thread_node,
                        EdgeProvenance::kSystem);
      // A reference points at the live object when there is one. When there is
      // not, the item stays and reads as unavailable: a durable reference a
      // person authored does not disappear because its target did.
      if (item.reference_live && !item.references_id.empty()) {
        assembler.AddEdge(
            EdgeType::kReferences, item_node,
            NodeId(item.references_kind, item.references_id),
            EdgeProvenance::kSystem);
      }
    }
  }

  for (const TaskInput& task : inputs.tasks) {
    assembler.AddNode(NodeKind::kTask, task.id, task.goal, std::string(),
                      task.state);
    const std::string task_node = NodeId(NodeKind::kTask, task.id);
    if (!task.window_id.empty()) {
      assembler.AddEdge(EdgeType::kBoundTo, task_node,
                        NodeId(NodeKind::kWindow, task.window_id),
                        EdgeProvenance::kSystem);
    }
    if (!task.thread_id.empty()) {
      assembler.AddEdge(EdgeType::kProducedBy, task_node,
                        NodeId(NodeKind::kThread, task.thread_id),
                        EdgeProvenance::kSystem);
    }
  }

  for (const BoardInput& board : inputs.boards) {
    assembler.AddNode(NodeKind::kBoard, board.id, board.name, std::string(),
                      NodeState::kLive);
    const std::string board_node = NodeId(NodeKind::kBoard, board.id);
    for (const auto& [kind, durable_id] : board.references) {
      // A Board's references are the person's own hand, so they carry user
      // provenance rather than system.
      assembler.AddEdge(EdgeType::kReferences, board_node,
                        NodeId(kind, durable_id), EdgeProvenance::kUser);
    }
  }

  std::string root_id;
  std::set<std::string> keep;
  switch (inputs.scope) {
    case GraphScope::kFocus:
      root_id = NodeId(NodeKind::kTab, inputs.focus_tab_id);
      keep = WithinDepth(assembler.pending(), root_id,
                         std::clamp<size_t>(inputs.focus_depth, 1u,
                                            kMaxFocusDepth));
      break;
    case GraphScope::kProject:
      root_id = NodeId(NodeKind::kThread, inputs.focus_thread_id);
      keep = WithinDepth(assembler.pending(), root_id, kMaxFocusDepth);
      break;
    case GraphScope::kSpace: {
      root_id = NodeId(NodeKind::kWorkspace, inputs.focus_workspace_id);
      // A Space is the workspace and what belongs to it, collected explicitly
      // rather than by walking outward from the workspace node. A walk would
      // reach a window and then every tab that window holds, including tabs
      // belonging to other Spaces - which is how this scope came to return the
      // entire profile and be indistinguishable from All Windows.
      keep.insert(root_id);
      std::set<std::string> tabs_here;
      for (const FolderInput& folder : inputs.folders) {
        if (folder.workspace_id == inputs.focus_workspace_id) {
          keep.insert(NodeId(NodeKind::kFolder, folder.id));
        }
      }
      for (const WindowInput& window : inputs.windows) {
        if (window.workspace_id == inputs.focus_workspace_id) {
          keep.insert(NodeId(NodeKind::kWindow, window.id));
        }
      }
      for (const TabInput& tab : inputs.tabs) {
        if (tab.workspace_id != inputs.focus_workspace_id) {
          continue;
        }
        const std::string tab_node = NodeId(NodeKind::kTab, tab.id);
        keep.insert(tab_node);
        tabs_here.insert(tab_node);
        if (!tab.split_id.empty()) {
          keep.insert(NodeId(NodeKind::kSplit, tab.split_id));
        }
        // The window holding a tab of this Space belongs to the picture even
        // if the window's own active workspace has since moved on.
        if (!tab.window_id.empty()) {
          keep.insert(NodeId(NodeKind::kWindow, tab.window_id));
        }
      }
      for (const ThreadInput& thread : inputs.threads) {
        const bool touches_space = std::ranges::any_of(
            thread.items, [&](const ContextItemInput& item) {
              return item.reference_live &&
                     item.references_kind == NodeKind::kTab &&
                     tabs_here.contains(
                         NodeId(NodeKind::kTab, item.references_id));
            });
        if (!touches_space) {
          continue;
        }
        keep.insert(NodeId(NodeKind::kThread, thread.id));
        for (const ContextItemInput& item : thread.items) {
          keep.insert(NodeId(NodeKind::kContextItem, item.id));
        }
      }
      for (const TaskInput& task : inputs.tasks) {
        const bool bound_here =
            (!task.window_id.empty() &&
             keep.contains(NodeId(NodeKind::kWindow, task.window_id))) ||
            (!task.thread_id.empty() &&
             keep.contains(NodeId(NodeKind::kThread, task.thread_id)));
        if (bound_here) {
          keep.insert(NodeId(NodeKind::kTask, task.id));
        }
      }
      for (const BoardInput& board : inputs.boards) {
        const bool references_space = std::ranges::any_of(
            board.references, [&](const auto& reference) {
              return keep.contains(
                  NodeId(reference.first, reference.second));
            });
        if (references_space) {
          keep.insert(NodeId(NodeKind::kBoard, board.id));
        }
      }
      break;
    }
    case GraphScope::kAllWindows:
      root_id = NodeId(NodeKind::kSession, kSessionId);
      break;
  }
  return assembler.Finish(inputs.scope, root_id, revision, keep);
}

}  // namespace seoul::context_map
