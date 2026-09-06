// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/context_map/context_map_builder.h"

#include <set>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::context_map {
namespace {

// Small named builders. The input structs carry out-of-line constructors, as
// Chromium style requires of anything holding strings, so they are not
// aggregates and cannot be brace-initialised by field name.
WorkspaceInput Workspace(std::string id, std::string name) {
  WorkspaceInput workspace;
  workspace.id = std::move(id);
  workspace.name = std::move(name);
  return workspace;
}

WindowInput Window(std::string id, std::string title, std::string workspace_id) {
  WindowInput window;
  window.id = std::move(id);
  window.title = std::move(title);
  window.workspace_id = std::move(workspace_id);
  return window;
}

FolderInput Folder(std::string id, std::string name, std::string workspace_id) {
  FolderInput folder;
  folder.id = std::move(id);
  folder.name = std::move(name);
  folder.workspace_id = std::move(workspace_id);
  return folder;
}

TabInput Tab(std::string id,
             std::string title,
             std::string origin,
             std::string window_id,
             std::string workspace_id,
             std::string folder_id,
             std::string split_id,
             bool active) {
  TabInput tab;
  tab.id = std::move(id);
  tab.title = std::move(title);
  tab.origin = std::move(origin);
  tab.window_id = std::move(window_id);
  tab.workspace_id = std::move(workspace_id);
  tab.folder_id = std::move(folder_id);
  tab.split_id = std::move(split_id);
  tab.active = active;
  return tab;
}

ContextItemInput Item(std::string id,
                      std::string title,
                      std::string subkind,
                      std::string references_id,
                      bool reference_live,
                      NodeKind references_kind = NodeKind::kTab) {
  ContextItemInput item;
  item.id = std::move(id);
  item.title = std::move(title);
  item.subkind = std::move(subkind);
  item.references_id = std::move(references_id);
  item.reference_live = reference_live;
  item.references_kind = references_kind;
  return item;
}

// A small but complete browser context: one Space, one window, three tabs, one
// folder, one split, a Project with a live and a dead reference, a task, and a
// Board. Every relationship the builder knows how to draw appears once.
GraphInputs SampleInputs() {
  GraphInputs inputs;
  inputs.scope = GraphScope::kSpace;
  inputs.focus_workspace_id = "ws1";
  inputs.workspaces.push_back(Workspace("ws1", "Research"));
  inputs.windows.push_back(Window("w1", "Window", "ws1"));
  inputs.folders.push_back(Folder("f1", "Sources", "ws1"));
  inputs.tabs.push_back(
      Tab("m1", "First", "example.org", "w1", "ws1", "f1", "s1", true));
  inputs.tabs.push_back(
      Tab("m2", "Second", "example.net", "w1", "ws1", "", "s1", false));
  inputs.tabs.push_back(Tab("m3", "Third", "", "w1", "ws1", "", "", false));

  ThreadInput thread;
  thread.id = "t1";
  thread.name = "Review";
  thread.items.push_back(Item("i1", "Open question", "note", "", true));
  thread.items.push_back(Item("i2", "Live source", "tab_reference", "m2", true));
  thread.items.push_back(
      Item("i3", "Closed source", "tab_reference", "gone", false));
  inputs.threads.push_back(thread);

  TaskInput task;
  task.id = "k1";
  task.goal = "Summarise";
  task.state = NodeState::kRunning;
  task.window_id = "w1";
  task.thread_id = "t1";
  inputs.tasks.push_back(task);

  BoardInput board;
  board.id = "b1";
  board.name = "Board";
  board.references.emplace_back(NodeKind::kTab, "m1");
  inputs.boards.push_back(board);
  return inputs;
}

const ContextMapNode* Find(const ContextMapGraph& graph, std::string_view id) {
  for (const ContextMapNode& node : graph.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

bool HasEdge(const ContextMapGraph& graph,
             EdgeType type,
             std::string_view source,
             std::string_view target) {
  for (const ContextMapEdge& edge : graph.edges) {
    if (edge.type == type && edge.source == source && edge.target == target) {
      return true;
    }
  }
  return false;
}

TEST(ContextMapBuilderTest, DrawsEveryDeterministicRelationship) {
  const ContextMapGraph graph = BuildContextMapGraph(SampleInputs(), 1);

  EXPECT_EQ(1u, graph.revision);
  EXPECT_EQ(GraphScope::kSpace, graph.scope);
  EXPECT_EQ("workspace:ws1", graph.root_id);

  ASSERT_TRUE(Find(graph, "tab:m1"));
  EXPECT_EQ(NodeState::kActive, Find(graph, "tab:m1")->state);
  EXPECT_EQ("example.org", Find(graph, "tab:m1")->subtitle);
  EXPECT_EQ(NodeState::kLive, Find(graph, "tab:m2")->state);

  EXPECT_TRUE(HasEdge(graph, EdgeType::kContains, "window:w1", "tab:m1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kBelongsTo, "tab:m1", "workspace:ws1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kActiveIn, "tab:m1", "window:w1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kGroupedIn, "tab:m1", "folder:f1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kSplitWith, "split:s1", "tab:m1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kSplitWith, "split:s1", "tab:m2"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kAttachedTo, "context_item:i1", "thread:t1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kReferences, "context_item:i2", "tab:m2"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kBoundTo, "task:k1", "window:w1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kProducedBy, "task:k1", "thread:t1"));
  EXPECT_TRUE(HasEdge(graph, EdgeType::kReferences, "board:b1", "tab:m1"))
      << "a Board reference must resolve, not be silently dropped";

  // A Board reference is the person's own hand, not something Seoul inferred.
  for (const ContextMapEdge& edge : graph.edges) {
    if (edge.source == "board:b1") {
      EXPECT_EQ(EdgeProvenance::kUser, edge.provenance);
    } else {
      EXPECT_EQ(EdgeProvenance::kSystem, edge.provenance);
    }
  }
  // Nothing is ever emitted as inferred in this version.
  for (const ContextMapEdge& edge : graph.edges) {
    EXPECT_NE(EdgeProvenance::kSuggested, edge.provenance);
  }
}

// A durable reference whose target is gone stays in the graph and reads as
// unavailable. Dropping it would quietly lose something the person authored.
TEST(ContextMapBuilderTest, StaleReferenceSurvivesWithoutADanglingEdge) {
  const ContextMapGraph graph = BuildContextMapGraph(SampleInputs(), 1);

  const ContextMapNode* stale = Find(graph, "context_item:i3");
  ASSERT_TRUE(stale);
  EXPECT_EQ(NodeState::kStale, stale->state);
  EXPECT_EQ("tab_reference", stale->subkind);
  EXPECT_TRUE(HasEdge(graph, EdgeType::kAttachedTo, "context_item:i3", "thread:t1"));

  // Every edge endpoint must be a node that is actually present.
  std::set<std::string> ids;
  for (const ContextMapNode& node : graph.nodes) {
    ids.insert(node.id);
  }
  for (const ContextMapEdge& edge : graph.edges) {
    EXPECT_TRUE(ids.contains(edge.source)) << edge.source;
    EXPECT_TRUE(ids.contains(edge.target)) << edge.target;
  }
}

// Identity comes from the durable id, so presentation changes do not create a
// new node and a move between Spaces does not either.
TEST(ContextMapBuilderTest, IdentityIsStableAcrossPresentationAndMovement) {
  const ContextMapGraph before = BuildContextMapGraph(SampleInputs(), 1);

  GraphInputs renamed = SampleInputs();
  renamed.tabs[0].title = "A completely different title";
  renamed.tabs[0].origin = "elsewhere.example";
  const ContextMapGraph after_rename = BuildContextMapGraph(renamed, 2);
  EXPECT_EQ(before.nodes.size(), after_rename.nodes.size());
  EXPECT_TRUE(Find(after_rename, "tab:m1"));

  GraphInputs moved = SampleInputs();
  moved.scope = GraphScope::kAllWindows;
  moved.workspaces.push_back(Workspace("ws2", "Other"));
  moved.tabs[0].workspace_id = "ws2";
  const ContextMapGraph after_move = BuildContextMapGraph(moved, 3);
  EXPECT_TRUE(Find(after_move, "tab:m1")) << "a move must not re-identify a tab";
  EXPECT_TRUE(HasEdge(after_move, EdgeType::kBelongsTo, "tab:m1", "workspace:ws2"));
  EXPECT_FALSE(HasEdge(after_move, EdgeType::kBelongsTo, "tab:m1", "workspace:ws1"));
}

// A Space must show ONE Space. Scoping is what separates this view from All
// Windows, and without it the view whose entire promise is "the Space I am in"
// returns the whole profile.
TEST(ContextMapBuilderTest, SpaceScopeExcludesEverySpaceButItsOwn) {
  GraphInputs inputs = SampleInputs();
  inputs.scope = GraphScope::kSpace;
  inputs.focus_workspace_id = "ws1";
  inputs.workspaces.push_back(Workspace("ws2", "Other"));
  inputs.windows.push_back(Window("w2", "Other window", "ws2"));
  inputs.tabs.push_back(
      Tab("m9", "Elsewhere", "other.example", "w2", "ws2", "", "", false));

  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  EXPECT_EQ("workspace:ws1", graph.root_id);
  EXPECT_TRUE(Find(graph, "workspace:ws1"));
  EXPECT_TRUE(Find(graph, "tab:m1"));
  EXPECT_FALSE(Find(graph, "workspace:ws2"))
      << "another Space must not appear in this one";
  EXPECT_FALSE(Find(graph, "tab:m9"));
  EXPECT_FALSE(Find(graph, "window:w2"));

  // And a tab that moves out of the Space leaves the picture.
  inputs.tabs[0].workspace_id = "ws2";
  const ContextMapGraph after = BuildContextMapGraph(inputs, 2);
  EXPECT_FALSE(Find(after, "tab:m1"));
}

// The totals are the whole justification for truncating at all. Counting edges
// after the node bound removed their endpoints would report a complete edge set
// at the exact moment edges were being discarded.
TEST(ContextMapBuilderTest, TruncationReportsEdgesItDiscarded) {
  GraphInputs inputs;
  inputs.scope = GraphScope::kAllWindows;
  inputs.windows.push_back(Window("w1", "Window", ""));
  for (size_t i = 0; i < kMaxGraphNodes + 200; ++i) {
    inputs.tabs.push_back(Tab("m" + std::to_string(i), "Tab", "", "w1", "ws1",
                              "", "", false));
  }
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  EXPECT_TRUE(graph.truncated);
  EXPECT_GT(graph.total_nodes, graph.nodes.size());
  EXPECT_GT(graph.total_edges, graph.edges.size())
      << "edges lost with their endpoints must still be counted";
}

// Under pressure the map must keep what a person made, not the 600th
// interchangeable tab. A stale authored item outranks a live one.
TEST(ContextMapBuilderTest, TruncationKeepsAuthoredContextOverAnonymousTabs) {
  GraphInputs inputs;
  inputs.scope = GraphScope::kAllWindows;
  inputs.windows.push_back(Window("w1", "Window", ""));
  for (size_t i = 0; i < kMaxGraphNodes + 200; ++i) {
    inputs.tabs.push_back(Tab("m" + std::to_string(i), "Tab", "", "w1", "ws1",
                              "", "", false));
  }
  ThreadInput thread;
  thread.id = "t1";
  thread.name = "Project";
  thread.items.push_back(Item("i1", "A note", "note", "", true));
  thread.items.push_back(
      Item("i2", "Source that closed", "tab_reference", "gone", false));
  inputs.threads.push_back(thread);
  BoardInput board;
  board.id = "b1";
  board.name = "Board";
  inputs.boards.push_back(board);

  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  ASSERT_TRUE(graph.truncated);
  EXPECT_TRUE(Find(graph, "thread:t1")) << "a Project must outlive spare tabs";
  EXPECT_TRUE(Find(graph, "context_item:i1"));
  EXPECT_TRUE(Find(graph, "context_item:i2"))
      << "a stale authored item is the thing least safe to drop";
  EXPECT_TRUE(Find(graph, "board:b1"));
}

// A reference may point at something that is not a tab. Assuming otherwise
// produces an edge to a node that does not exist, which is then dropped, so the
// item renders as live with nothing attached and no stale marker.
TEST(ContextMapBuilderTest, ReferencesResolveToTheirOwnKind) {
  GraphInputs inputs = SampleInputs();
  ThreadInput& thread = inputs.threads[0];
  thread.items.push_back(Item("i4", "Task output", "task_output", "k1", true,
                              NodeKind::kTask));
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  EXPECT_TRUE(HasEdge(graph, EdgeType::kReferences, "context_item:i4", "task:k1"))
      << "a task_output must reach its Task, not a tab that does not exist";
}

// A split needs two sides; one is a stale id or a half-applied snapshot.
TEST(ContextMapBuilderTest, ASplitOfOneIsNotDrawn) {
  GraphInputs inputs = SampleInputs();
  inputs.tabs[1].split_id = std::string();  // leaves m1 alone in split s1
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  EXPECT_FALSE(Find(graph, "split:s1"));
  for (const ContextMapEdge& edge : graph.edges) {
    EXPECT_NE(EdgeType::kSplitWith, edge.type);
  }
}

TEST(ContextMapBuilderTest, IsDeterministicAndFreeOfDuplicates) {
  const ContextMapGraph first = BuildContextMapGraph(SampleInputs(), 7);
  const ContextMapGraph second = BuildContextMapGraph(SampleInputs(), 7);

  ASSERT_EQ(first.nodes.size(), second.nodes.size());
  for (size_t i = 0; i < first.nodes.size(); ++i) {
    EXPECT_EQ(first.nodes[i].id, second.nodes[i].id) << "order must be stable";
  }
  ASSERT_EQ(first.edges.size(), second.edges.size());
  for (size_t i = 0; i < first.edges.size(); ++i) {
    EXPECT_EQ(first.edges[i].id, second.edges[i].id);
  }

  std::set<std::string> node_ids;
  for (const ContextMapNode& node : first.nodes) {
    EXPECT_TRUE(node_ids.insert(node.id).second) << "duplicate node " << node.id;
  }
  std::set<std::string> edge_ids;
  for (const ContextMapEdge& edge : first.edges) {
    EXPECT_TRUE(edge_ids.insert(edge.id).second) << "duplicate edge " << edge.id;
  }
}

// The same object described twice collapses to one node rather than appearing
// twice under the same id.
TEST(ContextMapBuilderTest, RepeatedInputCollapses) {
  GraphInputs inputs = SampleInputs();
  inputs.tabs.push_back(inputs.tabs[0]);
  inputs.workspaces.push_back(inputs.workspaces[0]);
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);

  size_t tabs = 0;
  for (const ContextMapNode& node : graph.nodes) {
    if (node.id == "tab:m1") {
      ++tabs;
    }
  }
  EXPECT_EQ(1u, tabs);
}

TEST(ContextMapBuilderTest, FocusShowsOnlyTheNeighbourhoodItWasAskedFor) {
  GraphInputs inputs = SampleInputs();
  inputs.scope = GraphScope::kFocus;
  inputs.focus_tab_id = "m1";
  inputs.focus_depth = 1;

  const ContextMapGraph near = BuildContextMapGraph(inputs, 1);
  EXPECT_EQ("tab:m1", near.root_id);
  EXPECT_TRUE(Find(near, "tab:m1"));
  EXPECT_TRUE(Find(near, "window:w1")) << "one hop away";
  EXPECT_FALSE(Find(near, "context_item:i1"))
      << "Focus at depth 1 must not show the whole profile";

  inputs.focus_depth = 3;
  const ContextMapGraph wide = BuildContextMapGraph(inputs, 2);
  EXPECT_GT(wide.nodes.size(), near.nodes.size());

  // Depth is clamped rather than trusted.
  inputs.focus_depth = 99;
  const ContextMapGraph clamped = BuildContextMapGraph(inputs, 3);
  EXPECT_EQ(wide.nodes.size(), clamped.nodes.size());
}

TEST(ContextMapBuilderTest, TruncationIsBoundedAndHonest) {
  GraphInputs inputs;
  inputs.scope = GraphScope::kSpace;
  inputs.focus_workspace_id = "ws1";
  inputs.workspaces.push_back(Workspace("ws1", "Space"));
  inputs.windows.push_back(Window("w1", "Window", "ws1"));
  const size_t requested = kMaxGraphNodes + 200;
  for (size_t i = 0; i < requested; ++i) {
    TabInput tab;
    tab.id = "m" + std::to_string(i);
    tab.title = "Tab";
    tab.window_id = "w1";
    tab.workspace_id = "ws1";
    tab.active = (i == requested - 1);
    inputs.tabs.push_back(tab);
  }

  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  EXPECT_TRUE(graph.truncated);
  EXPECT_LE(graph.nodes.size(), kMaxGraphNodes);
  EXPECT_LE(graph.edges.size(), kMaxGraphEdges);
  EXPECT_GT(graph.total_nodes, graph.nodes.size())
      << "the totals must say what was left out";

  // What survives is what answers the question: the scope root and the active
  // tab, never an arbitrary prefix.
  EXPECT_TRUE(Find(graph, "workspace:ws1"));
  EXPECT_TRUE(Find(graph, "tab:m" + std::to_string(requested - 1)))
      << "the active tab must survive truncation";
}

// Titles are clamped on a character boundary. Cutting a multi-byte sequence in
// half would put an invalid string on the wire that the schema cannot catch.
TEST(ContextMapBuilderTest, ClampsTitlesWithoutSplittingCharacters) {
  GraphInputs inputs = SampleInputs();
  inputs.tabs[0].title = std::string();
  for (size_t i = 0; i < kMaxNodeTitleLength; ++i) {
    inputs.tabs[0].title += "\xE2\x9C\x93";  // U+2713, three bytes.
  }
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  const ContextMapNode* tab = Find(graph, "tab:m1");
  ASSERT_TRUE(tab);
  EXPECT_LE(tab->title.size(), kMaxNodeTitleLength);
  EXPECT_EQ(0u, tab->title.size() % 3u) << "a character was cut in half";
}

TEST(ContextMapBuilderTest, EmptyInputStillProducesACoherentGraph) {
  GraphInputs inputs;
  inputs.scope = GraphScope::kAllWindows;
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 4);
  EXPECT_EQ(4u, graph.revision);
  EXPECT_EQ("session:session", graph.root_id);
  EXPECT_FALSE(graph.truncated);
  EXPECT_TRUE(graph.edges.empty());
  EXPECT_EQ(1u, graph.nodes.size()) << "the session node alone";
}

// Ids that cannot identify anything are dropped rather than producing a node
// with an empty id that no action could ever resolve.
TEST(ContextMapBuilderTest, RejectsUnidentifiableInput) {
  GraphInputs inputs;
  inputs.scope = GraphScope::kSpace;
  inputs.workspaces.push_back(Workspace(std::string(), "No id"));
  inputs.tabs.push_back(
      Tab(std::string(), "No id", "", "", "", "", "", false));
  const ContextMapGraph graph = BuildContextMapGraph(inputs, 1);
  for (const ContextMapNode& node : graph.nodes) {
    EXPECT_FALSE(node.id.empty());
    EXPECT_EQ(NodeKind::kSession, node.kind);
  }
}

}  // namespace
}  // namespace seoul::context_map
