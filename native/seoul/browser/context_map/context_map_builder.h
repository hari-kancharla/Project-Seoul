// Copyright 2026 The Project Seoul Authors
// The deterministic graph builder.
//
// A pure function from bounded snapshots to a graph. It takes plain data rather
// than the live services on purpose: the projection's correctness - stable
// identity, deterministic ordering, honest truncation - is exactly the part
// that must be provable without a browser window, and a coordinator that owns
// observers is the wrong place to prove it.
//
// The inputs below are what the owning services already expose, reduced to the
// fields a graph may carry. Nothing here accepts page content, credentials,
// history, or a raw native handle, so no caller can put one in a node.

#ifndef SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_BUILDER_H_
#define SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_BUILDER_H_

#include <string>
#include <utility>
#include <vector>

#include "seoul/browser/context_map/context_map_types.h"

namespace seoul::context_map {

struct WorkspaceInput {
  WorkspaceInput();
  WorkspaceInput(const WorkspaceInput&);
  WorkspaceInput& operator=(const WorkspaceInput&);
  WorkspaceInput(WorkspaceInput&&);
  WorkspaceInput& operator=(WorkspaceInput&&);
  ~WorkspaceInput();

  std::string id;
  std::string name;
};

struct WindowInput {
  WindowInput();
  WindowInput(const WindowInput&);
  WindowInput& operator=(const WindowInput&);
  WindowInput(WindowInput&&);
  WindowInput& operator=(WindowInput&&);
  ~WindowInput();

  std::string id;
  std::string title;
  // The workspace currently active in this window, if any.
  std::string workspace_id;
};

struct FolderInput {
  FolderInput();
  FolderInput(const FolderInput&);
  FolderInput& operator=(const FolderInput&);
  FolderInput(FolderInput&&);
  FolderInput& operator=(FolderInput&&);
  ~FolderInput();

  std::string id;
  std::string name;
  std::string workspace_id;
};

struct TabInput {
  TabInput();
  TabInput(const TabInput&);
  TabInput& operator=(const TabInput&);
  TabInput(TabInput&&);
  TabInput& operator=(TabInput&&);
  ~TabInput();

  // The durable membership id, not a session-scoped tab key: a graph node must
  // survive a relaunch and a move between Spaces.
  std::string id;
  std::string title;
  std::string origin;
  std::string window_id;
  std::string workspace_id;
  std::string folder_id;
  // Tabs sharing a split id are joined to each other.
  std::string split_id;
  bool active = false;
};

struct ContextItemInput {
  ContextItemInput();
  ContextItemInput(const ContextItemInput&);
  ContextItemInput& operator=(const ContextItemInput&);
  ContextItemInput(ContextItemInput&&);
  ContextItemInput& operator=(ContextItemInput&&);
  ~ContextItemInput();

  std::string id;
  std::string title;
  // note, excerpt, tab_reference, citation, task_output, workflow, decision.
  std::string subkind;
  // The object this item points at, when it points at one. The kind matters:
  // a task_output points at a Task and a citation may point at a Board, and
  // assuming every reference is a tab silently drops the edge for the rest.
  std::string references_id;
  NodeKind references_kind = NodeKind::kTab;
  // False when the referenced object no longer exists. The item still appears;
  // a durable reference the user authored does not vanish because its target
  // did, it is shown as unavailable.
  bool reference_live = true;
};

struct ThreadInput {
  ThreadInput();
  ThreadInput(const ThreadInput&);
  ThreadInput& operator=(const ThreadInput&);
  ThreadInput(ThreadInput&&);
  ThreadInput& operator=(ThreadInput&&);
  ~ThreadInput();

  std::string id;
  std::string name;
  std::vector<ContextItemInput> items;
};

struct TaskInput {
  TaskInput();
  TaskInput(const TaskInput&);
  TaskInput& operator=(const TaskInput&);
  TaskInput(TaskInput&&);
  TaskInput& operator=(TaskInput&&);
  ~TaskInput();

  std::string id;
  std::string goal;
  // running, finished, error.
  NodeState state = NodeState::kRunning;
  // The window this task is bound to, if any.
  std::string window_id;
  // The thread that produced it, if any.
  std::string thread_id;
};

struct BoardInput {
  BoardInput();
  BoardInput(const BoardInput&);
  BoardInput& operator=(const BoardInput&);
  BoardInput(BoardInput&&);
  BoardInput& operator=(BoardInput&&);
  ~BoardInput();

  std::string id;
  std::string name;
  // What this Board holds references to, by kind and durable id. The kind is
  // carried rather than assumed so the builder qualifies these exactly as it
  // qualifies every other id, instead of trusting the caller to pre-format.
  std::vector<std::pair<NodeKind, std::string>> references;
};

struct GraphInputs {
  GraphInputs();
  GraphInputs(const GraphInputs&);
  GraphInputs& operator=(const GraphInputs&);
  GraphInputs(GraphInputs&&);
  GraphInputs& operator=(GraphInputs&&);
  ~GraphInputs();

  GraphScope scope = GraphScope::kSpace;
  // The scope's centre. Which of these is read depends on the scope; the others
  // are ignored rather than guessed at.
  std::string focus_tab_id;
  std::string focus_workspace_id;
  std::string focus_thread_id;
  // 1..kMaxFocusDepth, clamped. Only meaningful for kFocus.
  size_t focus_depth = 1;

  std::vector<WorkspaceInput> workspaces;
  std::vector<WindowInput> windows;
  std::vector<FolderInput> folders;
  std::vector<TabInput> tabs;
  std::vector<ThreadInput> threads;
  std::vector<TaskInput> tasks;
  std::vector<BoardInput> boards;
};

// Builds the graph for `inputs`, stamped with `revision`.
//
// Deterministic: the same inputs always produce the same nodes, the same edges
// and the same order. Duplicate ids collapse rather than producing two nodes,
// and an edge whose endpoints are not both present is dropped rather than
// dangling.
ContextMapGraph BuildContextMapGraph(const GraphInputs& inputs,
                                     uint64_t revision);

}  // namespace seoul::context_map

#endif  // SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_BUILDER_H_
