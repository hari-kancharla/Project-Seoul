// Copyright 2026 The Project Seoul Authors
// The Context Map wire model: what a projected graph is made of.
//
// Context Map draws the browser context a person is working in - Spaces,
// windows, tabs, folders, splits, Projects, notes, tasks, Boards - and lets
// them navigate back to it or pin it somewhere durable. It owns none of that
// state. Every node here is a bounded rendering of an object another service
// owns, carrying an opaque id and nothing a renderer could use to reach past
// the graph.
//
// These enums are the canonical wire vocabulary. Their string forms are checked
// against protocol/context-map.schema.json by scripts/check-protocol.mjs, so the
// schema and the native model cannot drift apart.

#ifndef SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_TYPES_H_
#define SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seoul::context_map {

// What a node represents. Deliberately small: variation within a family is a
// subkind, so this enum does not explode every time a new kind of Context Item
// appears.
enum class NodeKind {
  kSession,
  kWorkspace,
  kWindow,
  kFolder,
  kTab,
  kSplit,
  kThread,
  kContextItem,
  kTask,
  kBoard,
  kArtifact,
  kSurface,
  kWorkflow,
  kLiveCollection,
};

// How a node currently stands. A durable reference whose backing object is gone
// is `kStale` - present and honest, not an error.
enum class NodeState {
  kLive,
  kActive,
  kStale,
  kRunning,
  kFinished,
  kError,
};

// Why an edge exists. Every edge in this version is evidence: either Seoul's
// own state says so (`kSystem`) or a person drew it (`kUser`). `kSuggested` is
// reserved for a future local-only relationship engine and is never emitted
// today, so nothing inferred can pass itself off as fact.
enum class EdgeProvenance {
  kSystem,
  kUser,
  kSuggested,
};

// The relationship an edge asserts. There is no generic "related": a
// deterministic system relationship always names what it actually is.
enum class EdgeType {
  kContains,
  kBelongsTo,
  kActiveIn,
  kGroupedIn,
  kSplitWith,
  kAttachedTo,
  kReferences,
  kBoundTo,
  kProducedBy,
  kUserLink,
};

// Which part of the browser context the graph is showing.
enum class GraphScope {
  kFocus,
  kSpace,
  kProject,
  kAllWindows,
};

const char* NodeKindToString(NodeKind kind);
const char* NodeStateToString(NodeState state);
const char* EdgeProvenanceToString(EdgeProvenance provenance);
const char* EdgeTypeToString(EdgeType type);
const char* GraphScopeToString(GraphScope scope);

// Bounds. Chosen against the limits the owning services already enforce, so the
// map cannot be asked to draw more than those services can hold.
inline constexpr size_t kMaxGraphNodes = 600;
inline constexpr size_t kMaxGraphEdges = 1800;
inline constexpr size_t kMaxFocusDepth = 3;
inline constexpr size_t kMaxNodeTitleLength = 200;
inline constexpr size_t kMaxNodeSubtitleLength = 120;

struct ContextMapNode {
  ContextMapNode();
  ContextMapNode(const ContextMapNode&);
  ContextMapNode& operator=(const ContextMapNode&);
  ContextMapNode(ContextMapNode&&);
  ContextMapNode& operator=(ContextMapNode&&);
  ~ContextMapNode();

  // Opaque outside the browser process. Derived from the owning service's
  // durable id where one exists, never from a title, a URL, or a position.
  std::string id;
  NodeKind kind = NodeKind::kTab;
  // Variation within a kind, for example the flavour of a Context Item.
  std::string subkind;
  std::string title;
  std::string subtitle;
  NodeState state = NodeState::kLive;
};

struct ContextMapEdge {
  ContextMapEdge();
  ContextMapEdge(const ContextMapEdge&);
  ContextMapEdge& operator=(const ContextMapEdge&);
  ContextMapEdge(ContextMapEdge&&);
  ContextMapEdge& operator=(ContextMapEdge&&);
  ~ContextMapEdge();

  std::string id;
  std::string source;
  std::string target;
  EdgeType type = EdgeType::kContains;
  EdgeProvenance provenance = EdgeProvenance::kSystem;
};

struct ContextMapGraph {
  ContextMapGraph();
  ContextMapGraph(const ContextMapGraph&);
  ContextMapGraph& operator=(const ContextMapGraph&);
  ContextMapGraph(ContextMapGraph&&);
  ContextMapGraph& operator=(ContextMapGraph&&);
  ~ContextMapGraph();

  // Monotonic per profile. A renderer must reject a snapshot older than the one
  // it is showing, so a slow reply cannot undo a newer state.
  uint64_t revision = 0;
  GraphScope scope = GraphScope::kSpace;
  // The node the scope is centred on, where the scope has one.
  std::string root_id;
  std::vector<ContextMapNode> nodes;
  std::vector<ContextMapEdge> edges;
  // True when the source graph exceeded the bounds above. The totals are what
  // the graph would have held, so the map can say what it left out instead of
  // presenting a truncated graph as complete.
  bool truncated = false;
  size_t total_nodes = 0;
  size_t total_edges = 0;
};

}  // namespace seoul::context_map

#endif  // SEOUL_BROWSER_CONTEXT_MAP_CONTEXT_MAP_TYPES_H_
