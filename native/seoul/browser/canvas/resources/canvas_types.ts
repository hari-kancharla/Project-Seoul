// Project Seoul Canvas wire types.
//
// These mirror the validated SAUI JSON that crosses the Mojo boundary. They
// intentionally describe data rather than DOM so rendering remains a closed,
// auditable transformation.

export interface SurfaceDoc {
  id: string;
  kind: string;
  title?: string;
  components: ComponentNode[];
  data?: Record<string, DataEntry>;
  actions?: SurfaceAction[];
}

export interface ComponentNode {
  id: string;
  type: string;
  props?: Record<string, unknown>;
  bindings?: Record<string, string>;
  accessible_name?: string;
  state?: string;
  state_message?: string;
  actions?: string[];
  children?: ComponentNode[];
}

export interface DataColumn {
  key: string;
  label: string;
}

export interface SeriesPoint {
  t_ms?: number;
  x?: number;
  y: number;
}

export interface DataEntry {
  kind: 'scalar'|'record'|'series'|'table';
  value?: unknown;
  fields?: Record<string, unknown>;
  columns?: DataColumn[];
  rows?: unknown[][];
  points?: SeriesPoint[];
}

export interface SurfaceAction {
  id: string;
  label: string;
  kind: string;
  target: string;
}

export interface TaskSnapshotDoc {
  id: string;
  goal: string;
  state: string;
  failure?: string;
  has_semantic_result?: boolean;
  pending_approval_step?: string;
  pending_approval_prompt?: string;
  pending_user_input?: boolean;
  receipts?: Array<{
    step_id: string;
    status: string;
    observed_summary?: string;
    verification?: {
      verified: boolean;
      method?: string;
      detail?: string;
    };
  }>;
}

export interface ThreadItemDoc {
  id: string;
  kind: string;
  title: string;
  reference: string;
  origin: string;
  text: string;
}

export interface ThreadSnapshotDoc {
  status: 'ready'|'error';
  detail?: string;
  id?: string;
  name?: string;
  archived?: boolean;
  items?: ThreadItemDoc[];
}

export interface PageContextDoc {
  status: 'ready'|'unavailable';
  tab_id: string;
  title: string;
  origin: string;
  customizable: boolean;
}

export interface LibraryBoardElementDoc {
  id: string;
  kind: 'text'|'link'|'image_reference'|'capture_reference'|'surface_reference';
  title: string;
  text: string;
  reference: string;
  origin: string;
  x: number;
  y: number;
  width: number;
  height: number;
  z_index: number;
}

export interface LibraryBoardDoc {
  id: string;
  name: string;
  archived: boolean;
  created_at?: unknown;
  modified_at?: unknown;
  elements?: LibraryBoardElementDoc[];
}

export interface LibraryArtifactDoc {
  id: string;
  kind: string;
  title: string;
  origin: string;
  mime_type: string;
  pinned: boolean;
}

export interface LiveCollectionDoc {
  id: string;
  name: string;
  refresh_capability: string;
  source_locator: string;
  refresh_interval_minutes: number;
  enabled: boolean;
  refresh_state: string;
  scope_available?: boolean;
  scope_current?: boolean;
  last_attempt_at_ms?: number;
  last_success_at_ms?: number;
  last_error?: string;
  items?: Array<{
    stable_key: string;
    title: string;
    subtitle?: string;
    url?: string;
    status?: string;
    actionable?: boolean;
  }>;
}

export interface LiveCollectionSourceDoc {
  id: string;
  name: string;
  description: string;
  provider: string;
  source_required: boolean;
  source_field: string;
  source_description: string;
  source_kind: 'text'|'url'|'unsupported';
}

export interface LibrarySnapshotDoc {
  schema_version?: number;
  status?: string;
  detail?: string;
  // Decimal uint64 emitted by the profile-owned Library service. Kept as a
  // string so freshness checks never lose precision in JavaScript.
  revision?: string;
  boards?: LibraryBoardDoc[];
  artifacts?: LibraryArtifactDoc[];
  live_collections?: LiveCollectionDoc[];
  live_collection_sources?: LiveCollectionSourceDoc[];
}

export interface StudioProviderRouteDoc {
  configured: boolean;
  healthy?: boolean;
  enabled?: boolean;
  available?: boolean;
  model_configured: boolean;
  discovered_model_count?: number;
  model?: string;
  voice_configured?: boolean;
}

export interface StudioSceneDoc {
  id: string;
  name: string;
  workspace_id: string;
  theme_id: string;
  site_layer_ids: string[];
  routing_rule_ids: string[];
  workflow_shortcut_ids: string[];
  lifecycle: {
    archive_temporary_tabs: boolean;
    idle_archive_minutes: number;
    restore_on_activation: boolean;
  };
  assistant: {
    allow_network: boolean;
    allow_cloud_models: boolean;
    max_sensitivity:
        'none'|'organization'|'page_content'|'personal'|
        'credential_adjacent';
    default_connectors: string[];
  };
  prefer_compact: boolean;
  active: boolean;
}

export interface StudioSiteLayerDoc {
  id: string;
  name: string;
  origin_pattern: string;
  scene_scope: string;
  enabled: boolean;
  adjustment_count: number;
}

export interface StudioThemeDoc {
  schema_version: number;
  id: string;
  name: string;
  scheme: 'light'|'dark'|'system';
  colors: {
    background: string;
    surface: string;
    text: string;
    muted_text: string;
    accent: string;
    accent_text: string;
    border: string;
    error: string;
  };
  typography: {
    font_family: string;
    base_size_px: number;
    scale_ratio: number;
    base_line_height_permille: number;
  };
  motion: {
    reduced_motion: boolean;
    reduced_transparency: boolean;
    base_duration_ms: number;
  };
  corner_radius_px: number;
  custom_colors?: Record<string, string>;
  active: boolean;
}

export interface StudioWorkspaceDoc {
  id: string;
  name: string;
  icon: string;
  archived: boolean;
}

export interface StudioEssentialDoc {
  id: string;
  name: string;
  root_url: string;
  icon: string;
  order: number;
}

export interface StudioRoutingRuleDoc {
  id: string;
  priority: number;
  match_type: 'anything'|'origin_exact'|'url_prefix'|'url_glob';
  pattern: string;
  source_workspace_id: string;
  require_user_gesture: boolean;
  disposition:
      'current_tab'|'new_temporary_tab'|'new_retained_tab'|
      'specific_workspace'|'preview'|'split_pane'|
      'external_application'|'ask_user';
  target_workspace_id: string;
  enabled: boolean;
}

export interface StudioWorkflowNodeDoc {
  id: string;
  kind: 'tool_step'|'approval'|'user_input';
  label: string;
  tool?: string;
  args?: Record<string, unknown>;
  prompt?: string;
  requires_approval?: boolean;
  max_iterations?: number;
}

export interface StudioWorkflowEdgeDoc {
  from: string;
  to: string;
  kind: 'sequence'|'on_success'|'on_failure'|'loop_back';
}

export interface StudioWorkflowDoc {
  schema_version: number;
  id: string;
  name: string;
  description: string;
  params: unknown[];
  nodes: StudioWorkflowNodeDoc[];
  edges: StudioWorkflowEdgeDoc[];
  trigger: {
    kind:
        'manual'|'schedule'|'scene_activation'|'navigation'|
        'page_state_change'|'service_event'|'startup';
    interval_minutes?: number;
    scene_id?: string;
    origin_pattern?: string;
    event_name?: string;
  };
  scene_scope?: string;
  site_scope?: string;
  version: number;
  created_at_ms: number;
  updated_at_ms: number;
}

export interface StudioCapabilityDoc {
  id: string;
  name: string;
  description: string;
  requires_network: boolean;
}

export interface StudioSnapshotDoc {
  schema_version?: number;
  status?: string;
  detail?: string;
  providers?: {
    local?: StudioProviderRouteDoc;
    cloud?: StudioProviderRouteDoc;
  };
  active_scene_id?: string;
  active_theme_id?: string;
  workspaces?: StudioWorkspaceDoc[];
  essentials?: StudioEssentialDoc[];
  scenes?: StudioSceneDoc[];
  themes?: StudioThemeDoc[];
  site_layers?: StudioSiteLayerDoc[];
  routing_rules?: StudioRoutingRuleDoc[];
  workflows?: StudioWorkflowDoc[];
  capabilities?: StudioCapabilityDoc[];
}

export interface SiteLayerAdjustmentDoc {
  kind: string;
  selectors?: string[];
  color_value?: string;
  font_family?: string;
  numeric_value?: number;
  density?: 'compact'|'comfortable'|'spacious';
}

export interface SiteLayerDoc {
  schema_version: number;
  id: string;
  name: string;
  origin_pattern: string;
  scene_scope: string;
  enabled: boolean;
  adjustments: SiteLayerAdjustmentDoc[];
  matches_active_page?: boolean;
}

export interface SiteLayerSnapshotDoc {
  status?: string;
  detail?: string;
  schema_version?: number;
  active_page?: {
    tab_id: string;
    title: string;
    origin: string;
    customizable: boolean;
  };
  matching_enabled_count?: number;
  layers?: SiteLayerDoc[];
}

export function safeHexColor(value: unknown): string {
  return typeof value === 'string' &&
          /^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$/.test(value) ?
      value : 'transparent';
}

export function propString(
    props: Record<string, unknown>|undefined, key: string): string {
  const value = props?.[key];
  return typeof value === 'string' ? value : '';
}

export function safeHttpUrl(value: unknown): string|undefined {
  if (typeof value !== 'string') {
    return undefined;
  }
  try {
    const url = new URL(value);
    return url.protocol === 'http:' || url.protocol === 'https:' ?
        url.href : undefined;
  } catch {
    return undefined;
  }
}
