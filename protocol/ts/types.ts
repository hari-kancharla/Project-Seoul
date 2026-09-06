// GENERATED FILE - do not edit.
// Derived from the canonical schemas in protocol/ by
// scripts/generate-protocol-types.mjs; drift is gated by
// scripts/check-protocol.mjs (npm run ci).

export const PROTOCOL_VERSION = 1;

// ---- semantic-result.schema.json ----

export type FieldId = string;

export type SemanticShape = "scalar" | "record" | "entity_collection" | "table" | "cube" | "series" | "time_series" | "interval_series" | "event_stream" | "status_stream" | "hierarchy" | "graph" | "geo_features" | "route" | "document" | "document_sections" | "citations" | "media" | "artifact" | "form_schema" | "action_set" | "diff" | "code_structure" | "composite";

export type SemanticRole = "none" | "identifier" | "name" | "description" | "measure" | "dimension" | "timestamp" | "interval_start" | "interval_end" | "duration" | "latitude" | "longitude" | "url" | "media_url" | "category" | "status" | "severity" | "money" | "percentage" | "count" | "open" | "high" | "low" | "close" | "parent_reference" | "source_node" | "target_node" | "body" | "citation_ref" | "mime_type";

export type FieldPrimitive = "string" | "integer" | "number" | "boolean" | "timestamp";

export type ValueClass = "categorical" | "ordinal" | "continuous" | "free_text";

export type FieldSensitivity = "public" | "personal" | "sensitive";

export interface FieldSpec {
  "id": FieldId;
  "label": string;
  "description"?: string;
  "primitive": FieldPrimitive;
  "nullable"?: boolean;
  "role"?: SemanticRole;
  "unit"?: string;
  "currency_code"?: string;
  "locale"?: string;
  "timezone"?: string;
  "format"?: string;
  "value_class"?: ValueClass;
  "confidence"?: number;
  "precision"?: number;
  "sensitivity"?: FieldSensitivity;
  "sortable"?: boolean;
  "filterable"?: boolean;
  "aggregatable"?: boolean;
}

export interface SemanticSchema {
  "schema_version": 1;
  "shape": SemanticShape;
  "fields"?: Array<FieldSpec>;
  /** Graph edges only. */
  "edge_fields"?: Array<FieldSpec>;
  /** Composite children; recursion bounded to depth 4 by the validators. */
  "parts"?: Array<SemanticSchema>;
  "part_names"?: Array<FieldId>;
}

export interface Citation {
  "url": string;
  "title"?: string;
}

export type FreshnessState = "real_time" | "delayed" | "cached" | "stale";

export interface SemanticProvenance {
  "source_name": string;
  "source_url"?: string;
  "retrieved_at_ms": number;
  "effective_at_ms"?: number;
  "timezone"?: string;
  "units"?: string;
  "freshness"?: FreshnessState;
  "completeness"?: number;
  "provider"?: string;
  "transformations"?: Array<string>;
  "citations"?: Array<Citation>;
}

export interface SemanticError {
  "code": string;
  "message": string;
}

export interface SourceConflict {
  "field_id": FieldId;
  "source_a": string;
  "source_b": string;
  "note"?: string;
}

/** The versioned, domain-neutral result model every Seoul capability outcome uses. Mirrors native/seoul/browser/semantic/semantic_types.h exactly: a shape plus fields whose meaning is carried by semantic roles and metadata. Field-level bounds beyond structure (row limits, finite numbers, role coherence per shape, undeclared-key rejection) are enforced by the semantic validators on each side, not by this schema. */
export interface SemanticResult {
  "schema": SemanticSchema;
  /** Shape-specific layout. scalar: one primitive. record/document/artifact/diff: dict keyed by field id. List shapes: list of dicts keyed by field id. graph: {nodes: [...], edges: [...]}. composite: dict keyed by part name. */
  "data": unknown;
  "provenance": SemanticProvenance;
  /** ResultState. Defaults to complete when absent. */
  "state"?: "complete" | "partial" | "streaming" | "failed";
  /** Pagination continuation; empty or absent when none. */
  "continuation_token"?: string;
  /** Fields the source could not supply. They must be absent from the data: gaps are reported, never fabricated. */
  "unavailable_field_ids"?: Array<FieldId>;
  "errors"?: Array<SemanticError>;
  "conflicts"?: Array<SourceConflict>;
}

// ---- adaptive-surface.schema.json ----

export type SurfaceUuid = string;

export type SauiIdentifier = string;

export type SurfaceKind = "response" | "form" | "dashboard" | "report" | "approval" | "workflow_canvas" | "task_status" | "monitor";

export type ComponentType = "text" | "rich_text" | "heading" | "divider" | "badge" | "icon" | "image" | "link" | "citation" | "source_list" | "progress" | "spinner" | "error_state" | "empty_state" | "stack" | "row" | "grid" | "tabs" | "collapsible_section" | "resizable_panel" | "carousel" | "detail_drawer" | "button" | "segmented_control" | "checkbox" | "radio_group" | "select" | "text_input" | "numeric_input" | "date_input" | "date_range_input" | "time_input" | "slider" | "search_field" | "file_picker" | "confirmation" | "schema_form" | "key_value_card" | "metric" | "entity_card" | "list" | "table" | "sortable_table" | "comparison_matrix" | "timeline" | "activity_log" | "tree" | "pagination" | "filter_chips" | "document" | "media" | "file" | "line_chart" | "area_chart" | "bar_chart" | "stacked_bar_chart" | "scatter_chart" | "pie_chart" | "candlestick_chart" | "range_chart" | "sparkline" | "histogram" | "heat_map" | "network_graph" | "map" | "geo_layer" | "map_marker_list" | "task_graph" | "workflow_node" | "workflow_edge" | "trigger_card" | "approval_request" | "execution_status" | "result_card" | "retry_control" | "plan_view" | "current_step" | "action_receipt" | "blocker_list" | "cost_summary" | "provider_indicator" | "code_block" | "diff_view" | "diagnostic_list" | "log_viewer" | "stack_trace" | "file_tree";

export type ComponentState = "ready" | "loading" | "error" | "empty";

export type UpdatePolicy = "static" | "live";

/** Primitive prop value, or a bounded structured list for the keys options/columns/chips/sources/segments (items are short strings or flat dicts of strings; sources items require an http(s) href; columns items require a key). */
export type PropValue = string | number | boolean | Array<string | Record<string, string>>;

export type Props = Record<string, PropValue>;

export interface ComponentNode {
  "id": SauiIdentifier;
  "type": ComponentType;
  "props"?: Props;
  /** slot name -> data entry name. The primary slot is "data". */
  "bindings"?: Record<string, SauiIdentifier>;
  "accessible_name"?: string;
  "state"?: ComponentState;
  "state_message"?: string;
  "update_policy"?: UpdatePolicy;
  "actions"?: Array<SauiIdentifier>;
  /** Depth bounded to 12 by the parsers. */
  "children"?: Array<ComponentNode>;
}

export interface DataProvenance {
  "source_name": string;
  "source_url"?: string;
  "retrieved_at_ms"?: number;
  "effective_at_ms"?: number;
  "timezone"?: string;
  "units"?: string;
  "freshness"?: "real_time" | "delayed" | "cached" | "stale";
  "completeness"?: number;
}

export interface SeriesPoint {
  "y": number;
  /** Epoch milliseconds; time-indexed points. */
  "t_ms"?: number;
  /** Used when t_ms is absent. */
  "x"?: number;
}

export interface DataEntry {
  "kind": "scalar" | "record" | "series" | "table";
  /** kind=scalar: one primitive (string/number/boolean). */
  "value"?: unknown;
  /** kind=record: one flat dictionary of primitives. */
  "fields"?: Record<string, unknown>;
  /** kind=series. */
  "points"?: Array<SeriesPoint>;
  "x_unit"?: string;
  "y_unit"?: string;
  /** kind=table. */
  "columns"?: Array<{
    "key": string;
    "label": string;
  }>;
  /** kind=table: rows of primitive cells, one per column. */
  "rows"?: Array<unknown[]>;
  "provenance"?: DataProvenance;
}

export type SurfaceActionKind = "tool_call" | "local_state" | "workflow_edit" | "browser_action" | "task_approval" | "navigate";

export interface SurfaceAction {
  "id": SauiIdentifier;
  "label": string;
  "kind": SurfaceActionKind;
  "target": string;
  "payload"?: Record<string, unknown>;
  "requires_confirmation"?: boolean;
}

export interface SurfaceProvenance {
  /** "deterministic" or "model:<provider>/<model>". */
  "generator": string;
  "created_at_ms"?: number;
  "source_urls"?: Array<string>;
}

/** The SAUI declarative surface document. Mirrors native/seoul/browser/saui/saui_document.cc ParseSurface/SurfaceToValue exactly. No component carries executable code, raw HTML, or event handlers. Per-key prop bounds beyond structure (url-key scheme checks, code-key length, structured-list item rules) are enforced by the SAUI parser/validator on each side. `revision` is runtime state and never travels on the wire. */
export interface AdaptiveSurface {
  "schema_version": 1;
  "id"?: SurfaceUuid;
  "kind": SurfaceKind;
  "title"?: string;
  "components": Array<ComponentNode>;
  "data"?: Record<string, DataEntry>;
  "actions"?: Array<SurfaceAction>;
  "provenance"?: SurfaceProvenance;
  "pinned"?: boolean;
}

// ---- surface-patch.schema.json ----

export type PatchOp = SetProps | SetState | SetTitle | SetBindings | UpsertDataEntry | AppendSeriesPoints | AppendChild | RemoveComponent | ReplaceComponent | SetActions;

export interface SetProps {
  "op": "set_props";
  "target": SauiIdentifier;
  "props": Props;
}

export interface SetState {
  "op": "set_state";
  "target": SauiIdentifier;
  "state": ComponentState;
  "message"?: string;
}

export interface SetTitle {
  "op": "set_title";
  "title": string;
}

export interface SetBindings {
  "op": "set_bindings";
  "target": SauiIdentifier;
  /** Replaces the component's binding map. Every referenced entry must exist and satisfy the component's accepted binding kinds. */
  "bindings": Record<string, SauiIdentifier>;
}

export interface UpsertDataEntry {
  "op": "upsert_data_entry";
  "entry": SauiIdentifier;
  "value": DataEntry;
}

export interface AppendSeriesPoints {
  "op": "append_series_points";
  "entry": SauiIdentifier;
  "points": Array<SeriesPoint>;
}

export interface AppendChild {
  "op": "append_child";
  /** A container component id. */
  "target": SauiIdentifier;
  "component": ComponentNode;
}

export interface RemoveComponent {
  "op": "remove_component";
  "target": SauiIdentifier;
}

export interface ReplaceComponent {
  "op": "replace_component";
  "target": SauiIdentifier;
  "component": ComponentNode;
}

export interface SetActions {
  "op": "set_actions";
  "actions": Array<SurfaceAction>;
}

/** Typed incremental surface updates addressed by stable component and data-entry ids. Mirrors native/seoul/browser/saui/saui_patch.cc ParseSurfacePatch exactly, including the set_bindings operation. A patch applies atomically: if any operation or the resulting surface is invalid, the surface is left untouched. Patches carry no protocol version of their own; they apply to a surface whose document is versioned. */
export interface SurfacePatch {
  "surface_id": SurfaceUuid;
  "ops": Array<PatchOp>;
}

// ---- component-event.schema.json ----

/** A typed event emitted by the Canvas when the user interacts with a rendered component. Mirrors native/seoul/browser/saui/saui_events.h. Events carry stable component/action ids and a typed value; the Canvas never emits raw DOM state or click coordinates. Events carry no protocol version of their own; they reference a versioned surface. */
export interface ComponentEvent {
  "surface_id": SurfaceUuid;
  "component_id": SauiIdentifier;
  "kind": "activate" | "value_changed" | "submit" | "select" | "dismiss";
  /** The surface action triggered by this event, when the component declared one. Resolution happens in the Canvas session layer against the surface's validated action list. */
  "action_id"?: SauiIdentifier;
  /** kind=value_changed: the new value. kind=submit: collected field values. kind=select: the selection key. Absent otherwise. */
  "value"?: unknown;
}

// ---- task-snapshot.schema.json ----

export type StepStatus = "pending" | "awaiting_approval" | "awaiting_input" | "running" | "succeeded" | "failed" | "outcome_unknown" | "skipped" | "cancelled";

export type ExecutionRoute = "deterministic" | "local" | "cloud";

export interface VerificationRecord {
  "verified": boolean;
  /** "postcondition", "observation", "verifier:<id>", or "fixture_contract" for synthetic Design Lab fixtures. Never claim observation for a fixture. */
  "method": string;
  "detail"?: string;
}

export interface ActionReceipt {
  "step_id": string;
  /** ToolId: namespaced dotted identifier, two to four segments. */
  "tool": string;
  "status": StepStatus;
  "started_at_ms"?: number;
  "finished_at_ms"?: number;
  /** What actually happened, from observation. For fixture execution this must describe the fixture, never a real browser state. */
  "observed_summary"?: string;
  "verification": VerificationRecord;
  "route": ExecutionRoute;
  "cost_microdollars"?: number;
  "model_calls"?: number;
  "navigations"?: number;
}

export interface BudgetUsage {
  "steps_executed"?: number;
  "model_calls"?: number;
  "navigations"?: number;
  "cloud_cost_microdollars"?: number;
  "replans_used"?: number;
}

/** A read snapshot of one task for rendering: state, plan origin, bounded action receipts, budget usage, approval state, and window association. Mirrors native/seoul/browser/product/task_service.h TaskSnapshot and native/seoul/browser/tasks/task_types.h. This is the typed document the Canvas renders task progress from; it replaces free-form status strings. */
export interface TaskSnapshot {
  "schema_version": 1;
  "id": string;
  "goal": string;
  "state": "draft" | "planning" | "awaiting_approval" | "executing" | "paused" | "monitoring" | "completed" | "failed" | "cancelled";
  "failure"?: "none" | "step_failed" | "budget_exhausted" | "assumption_invalid" | "user_stopped" | "provider_unavailable";
  "plan_origin"?: "deterministic" | "local" | "cloud";
  "receipts"?: Array<ActionReceipt>;
  "usage"?: BudgetUsage;
  /** Non-empty while awaiting approval or typed user input. */
  "pending_approval_step"?: string;
  "pending_approval_prompt"?: string;
  /** True when the pending interaction requires typed user input rather than approval. */
  "pending_user_input"?: boolean;
  "has_semantic_result"?: boolean;
  /** LiveWindowKey wire form ("w-<session_id>"). Absent when the task is not window-bound. */
  "window"?: string;
  "replans_used"?: number;
}

// ---- capability-descriptor.schema.json ----

export interface ToolSchema {
  "type": "object";
  "properties"?: Record<string, SchemaField>;
  "required"?: Array<string>;
}

export interface SchemaField {
  "type"?: "string" | "integer" | "number" | "boolean" | "array" | "object";
  "description"?: string;
  "enum"?: Array<string>;
  "format"?: "uri";
  "minimum"?: number;
  "maximum"?: number;
  "maxLength"?: number;
  "items"?: SchemaField;
  "properties"?: Record<string, SchemaField>;
  "required"?: Array<string>;
}

/** One typed operation Seoul can plan with, regardless of who supplies it. Mirrors native/seoul/browser/tools/tool_types.h ToolDescriptor. "Tool" and "capability" are the same concept; the id is the stable CapabilityId. Input/output schemas use the same bounded JSON-Schema subset native/seoul/browser/tools/tool_schema_from_json.cc accepts. */
export interface CapabilityDescriptor {
  "schema_version": 1;
  /** Namespaced dotted identifier, two to four segments. */
  "id": string;
  /** The descriptor's own contract version; bump on breaking schema changes. */
  "version"?: number;
  "name": string;
  /** Planner-facing behavior contract. */
  "description": string;
  /** "seoul" for builtins; connector id otherwise. Synthetic Design Lab fixtures use "fixture". */
  "provider": string;
  "input_schema"?: ToolSchema;
  "output_schema"?: ToolSchema;
  "capability_tags"?: Array<string>;
  "requires_network"?: boolean;
  "sensitivity"?: "none" | "organization" | "page_content" | "personal" | "credential_adjacent";
  "risk"?: "read_only" | "reversible_mutation" | "irreversible_mutation" | "external_side_effect";
  "approval"?: "never_required" | "first_use_per_scope" | "always_required";
  "timeout_ms"?: number;
  "cancellable"?: boolean;
  "supports_streaming"?: boolean;
  "idempotency"?: "idempotent" | "not_idempotent";
  "freshness"?: "real_time" | "near_real_time" | "cached" | "static";
  "retry"?: {
    "max_attempts"?: number;
    "backoff_ms"?: number;
  };
  /** What observable state change proves the tool ran. Empty for pure reads; for fixtures this states that no real observation exists. */
  "observation_contract"?: string;
  "verifier_id"?: string;
  /** Optional SAUI component wire name best suited to render results. */
  "preferred_component"?: ComponentType;
}

// ---- context-map.schema.json ----

export type GraphScope = "focus" | "space" | "project" | "all_windows";

export type NodeKind = "session" | "workspace" | "window" | "folder" | "tab" | "split" | "thread" | "context_item" | "task" | "board" | "artifact" | "surface" | "workflow" | "live_collection";

export type NodeState = "live" | "active" | "stale" | "running" | "finished" | "error";

export type EdgeType = "contains" | "belongs_to" | "active_in" | "grouped_in" | "split_with" | "attached_to" | "references" | "bound_to" | "produced_by" | "user_link";

/** Why the edge exists. Every edge in this version is evidence: Seoul's own state (system) or a person's own hand (user). 'suggested' is reserved for a future local-only relationship engine and is never emitted today, so nothing inferred can present itself as fact. */
export type EdgeProvenance = "system" | "user" | "suggested";

export interface Node {
  /** Opaque outside the browser process. Derived from the owning service's durable id where one exists, never from a title, a URL, or a position. */
  "id": string;
  "kind": NodeKind;
  /** Variation within a kind, so the kind enum does not grow every time a new flavour of Context Item appears. */
  "subkind"?: string;
  "title": string;
  "subtitle"?: string;
  "state": NodeState;
}

export interface Edge {
  "id": string;
  "source": string;
  "target": string;
  "type": EdgeType;
  "provenance": EdgeProvenance;
}

/** One rendering of the browser context a person is working in: Spaces, windows, tabs, folders, splits, Projects, notes, tasks, Boards and artifacts, with the deterministic relationships between them. Mirrors native/seoul/browser/context_map/context_map_types.h. This document is a PROJECTION: it owns none of the objects it draws, carries only bounded presentation fields plus an opaque id, and never carries page content, credentials, history or anything a renderer could use to reach past the graph. */
export interface ContextMapGraph {
  "schema_version": 1;
  /** Monotonic per profile, as decimal digits. A renderer must ignore a snapshot whose revision is lower than the one it is already showing, so a slow reply cannot undo newer state. Carried as a string, not a number, for two reasons: the native counter is 64-bit and would lose precision as a JSON number, and the Canvas renderer's existing ordering guard accepts only a digit string - a number silently disables the comparison and lets a stale snapshot through. */
  "revision": string;
  "scope": GraphScope;
  /** The node the scope is centred on, where the scope has one. */
  "root_id"?: string;
  "nodes": Array<Node>;
  "edges": Array<Edge>;
  /** True when the source graph exceeded the render bounds. The totals below are what it would have held, so the map can state what it left out rather than presenting a partial graph as complete. */
  "truncated": boolean;
  "total_nodes": number;
  "total_edges": number;
}
