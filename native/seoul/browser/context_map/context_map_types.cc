// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/context_map/context_map_types.h"

namespace seoul::context_map {

ContextMapNode::ContextMapNode() = default;
ContextMapNode::ContextMapNode(const ContextMapNode&) = default;
ContextMapNode& ContextMapNode::operator=(const ContextMapNode&) = default;
ContextMapNode::ContextMapNode(ContextMapNode&&) = default;
ContextMapNode& ContextMapNode::operator=(ContextMapNode&&) = default;
ContextMapNode::~ContextMapNode() = default;

ContextMapEdge::ContextMapEdge() = default;
ContextMapEdge::ContextMapEdge(const ContextMapEdge&) = default;
ContextMapEdge& ContextMapEdge::operator=(const ContextMapEdge&) = default;
ContextMapEdge::ContextMapEdge(ContextMapEdge&&) = default;
ContextMapEdge& ContextMapEdge::operator=(ContextMapEdge&&) = default;
ContextMapEdge::~ContextMapEdge() = default;

ContextMapGraph::ContextMapGraph() = default;
ContextMapGraph::ContextMapGraph(const ContextMapGraph&) = default;
ContextMapGraph& ContextMapGraph::operator=(const ContextMapGraph&) = default;
ContextMapGraph::ContextMapGraph(ContextMapGraph&&) = default;
ContextMapGraph& ContextMapGraph::operator=(ContextMapGraph&&) = default;
ContextMapGraph::~ContextMapGraph() = default;

const char* NodeKindToString(NodeKind kind) {
  switch (kind) {
    case NodeKind::kSession:
      return "session";
    case NodeKind::kWorkspace:
      return "workspace";
    case NodeKind::kWindow:
      return "window";
    case NodeKind::kFolder:
      return "folder";
    case NodeKind::kTab:
      return "tab";
    case NodeKind::kSplit:
      return "split";
    case NodeKind::kThread:
      return "thread";
    case NodeKind::kContextItem:
      return "context_item";
    case NodeKind::kTask:
      return "task";
    case NodeKind::kBoard:
      return "board";
    case NodeKind::kArtifact:
      return "artifact";
    case NodeKind::kSurface:
      return "surface";
    case NodeKind::kWorkflow:
      return "workflow";
    case NodeKind::kLiveCollection:
      return "live_collection";
  }
  return "tab";
}

const char* NodeStateToString(NodeState state) {
  switch (state) {
    case NodeState::kLive:
      return "live";
    case NodeState::kActive:
      return "active";
    case NodeState::kStale:
      return "stale";
    case NodeState::kRunning:
      return "running";
    case NodeState::kFinished:
      return "finished";
    case NodeState::kError:
      return "error";
  }
  return "live";
}

const char* EdgeProvenanceToString(EdgeProvenance provenance) {
  switch (provenance) {
    case EdgeProvenance::kSystem:
      return "system";
    case EdgeProvenance::kUser:
      return "user";
    case EdgeProvenance::kSuggested:
      return "suggested";
  }
  return "system";
}

const char* EdgeTypeToString(EdgeType type) {
  switch (type) {
    case EdgeType::kContains:
      return "contains";
    case EdgeType::kBelongsTo:
      return "belongs_to";
    case EdgeType::kActiveIn:
      return "active_in";
    case EdgeType::kGroupedIn:
      return "grouped_in";
    case EdgeType::kSplitWith:
      return "split_with";
    case EdgeType::kAttachedTo:
      return "attached_to";
    case EdgeType::kReferences:
      return "references";
    case EdgeType::kBoundTo:
      return "bound_to";
    case EdgeType::kProducedBy:
      return "produced_by";
    case EdgeType::kUserLink:
      return "user_link";
  }
  return "contains";
}

const char* GraphScopeToString(GraphScope scope) {
  switch (scope) {
    case GraphScope::kFocus:
      return "focus";
    case GraphScope::kSpace:
      return "space";
    case GraphScope::kProject:
      return "project";
    case GraphScope::kAllWindows:
      return "all_windows";
  }
  return "space";
}

}  // namespace seoul::context_map
