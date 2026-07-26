# Seoul Studio Specification

Status: The runtime index, provider routes, Essentials, Themes, Scenes, routing
rules, and workflow authoring are implemented and browser-verified. Boost
editing remains in its dedicated Canvas view. Exact evidence is in the product
readiness report.

Studio is the Canvas area for inspecting and editing the systems that shape how
Seoul behaves: reasoning routes, global Essentials, Scenes, Themes, routing,
and workflows. It is backed by the live profile runtime, not a settings mockup
or fixture catalog.
Site Layers are authored as Boosts in their page-focused Canvas view so the
scope and rollback behavior stay visible beside the affected page.

## Typed runtime projection

`PageHandler.GetStudioSnapshot` returns a bounded JSON document over Mojo from
the Canvas's exact window-bound `SeoulRuntimeService`. The browser constructs it
from three authoritative owners:

- `ProviderRegistry::Snapshot()` supplies local/cloud configuration and
  availability flags.
- `SceneRegistry::List()` supplies id-ordered Scene metadata.
- `ThemeRegistry::List()` supplies id-ordered, accessibility-validated Theme
  metadata and preview tokens.
- `SiteLayerRegistry::List()` supplies id-ordered, validated layer metadata.
- `OrganizationModel` supplies workspaces, global Essentials, and
  priority-ordered routing rules.
- `WorkflowService` supplies validated workflow graphs and trigger state.

The renderer performs only a closed Lit template mapping. It does not receive
HTML, CSS, JavaScript, DOM state, or inferred sample entries. Empty registries
produce explicit empty states rather than invented presets.

## Data minimization

The snapshot exposes provider booleans, model identifiers and counts, Scene
name/reference counts, validated Theme preview colors/preferences, and Site
Layer names/scopes/counts. It excludes credentials, stored local endpoint URLs,
raw provider errors, browsing content, and capture bytes. Secrets are
write-only from the renderer and go directly to the browser's credential
store. Studio is profile-owned but still uses the Canvas window binding so a
tab-loaded or stale Canvas fails closed.

## Provider route mutations

The local route editor accepts only loopback endpoints and a model id. The
stored endpoint is never reflected back across Mojo; reconfiguration requires
the user to enter it again. The health action calls the configured loopback
server through the native provider registry.

The cloud editor stores reasoning and Realtime voice credentials in macOS
Keychain and returns only configured/available booleans. Clearing the route
requires an explicit confirmation and removes both settings and stored Seoul
credentials.

## Typed authoring and activation

Essential authoring accepts only a bounded name and an HTTP(S) destination.
The browser canonicalizes the URL and refuses another Essential for the same
origin. “Keep current page” captures the browser-observed page title and origin;
manual editing remains available. Essentials persist profile-wide, appear in
every Workspace, reuse a matching live tab, and can be removed without closing
that tab.

Theme authoring creates and edits validated product tokens, including color,
font, density, transparency, and motion preferences. Applying a Theme is bound
to the exact Canvas window. A Theme cannot be deleted while an active Scene or
another stored definition still depends on it.

Scene authoring binds an existing workspace to an optional Theme, Site Layers,
routing rules, workflow shortcuts, lifecycle policy, connector defaults,
assistant policy, and compact-chrome preference. Activation switches the real
workspace, applies the Theme and compact policy, refreshes scoped Site Layers,
restores eligible archived tabs in the background, and starts matching
Scene-activation workflows. Leaving a Scene restores the exact pre-Scene Theme
and compact state. Runtime reconciliation clears a Scene if another command
moves the window away from its workspace. Standalone compact mode remains a
Workspace-owned shell preference outside Studio; Scene activation temporarily
overrides it through the same window-local native controller and restores it
without changing another browser window.

Routing authoring supports typed source/match/priority/disposition fields.
Referenced rules cannot be deleted while a Scene uses them. The real navigation
path resolves current/new temporary/new retained/specific-workspace/Preview/
split/external destinations; ask-user rules enter the approval path.

Workflow authoring creates bounded typed graphs, trigger settings, parameters,
nodes, and edges. Studio can run, duplicate, edit, and delete workflows, with
reference validation and dependency guards. Execution uses the same planner,
permission, approval, receipt, and failure paths as a manually started task.

All catalogs persist through profile preferences. Restoration loads referenced
catalogs in dependency order and prunes invalid Scene/workflow references to a
finite valid fixpoint instead of retaining latent activation failures.
An active durable Scene must be cleared before its definition can be edited;
this prevents a Studio save from silently changing live Workspace, Theme,
compact, lifecycle, routing, or workflow policy outside an activation
transaction.

## Deliberate boundaries

Studio never accepts executable JavaScript, remote UI markup, or raw CSS.
Credentialed cloud/realtime acceptance still requires the user's endpoint,
account, microphone permission, and hardware. Active Scene/Theme presentation
intent is durable per Workspace and is rebound to one matching live window
after relaunch; distinct simultaneous presentations for multiple windows on
the same Workspace are not inferred from regenerated Chromium window ids.
