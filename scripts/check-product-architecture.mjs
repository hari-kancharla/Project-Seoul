#!/usr/bin/env node
// Product-architecture gate for the Seoul native product runtime.
//
// Enforces the vertical-integration invariants the product depends on, so a
// regression that quietly unwires the runtime fails CI instead of shipping:
//
//   1. No giant capability dispatch switch: capability execution must go
//      through the CapabilityExecutorRegistry, never a growing if/else-if or
//      switch on capability id in the task/runtime layer.
//   2. No reinterpreting-default: an unknown capability/action must fail
//      closed, never fall through to a default action.
//   3. No placeholder production callbacks: no "TODO"/"not implemented"/
//      "fake success" markers in product production source.
//   4. No fixed workspace names: the shell must not create workspaces named a
//      hardcoded string or rename by appending a fixed suffix.
//   5. No empty browser-test bodies anywhere under native/seoul.
//   6. Every registered CapabilityExecutor subclass is constructed somewhere
//      (registered), and the runtime marks executor-less descriptors
//      unavailable.
//   7. Canvas/product turns must use an explicit WindowRuntimeBinding token,
//      never an active/last-focused-window fallback.
//   8. Canvas voice must use the realtime voice session bridge, not the
//      legacy local speech button path.
//   9. Shipping Canvas rendering must remain Chromium Lit with a checked-in
//      template and complete local coverage of the accepted visual catalog.
//  10. Preview lifecycle must remain ephemeral and require explicit two-phase
//      promotion; opening a replacement may not destroy known-good state before
//      the new preview is admitted.
//
// Pure static scan over tracked source; no build required.

import fs from 'node:fs';
import path from 'node:path';

const repoRoot = path.resolve(import.meta.dirname, '..');
const productRoot = path.join(repoRoot, 'native/seoul/browser/product');
const seoulRoot = path.join(repoRoot, 'native/seoul');
const problems = [];

function walk(dir, filter) {
  const out = [];
  if (!fs.existsSync(dir)) return out;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const abs = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      out.push(...walk(abs, filter));
    } else if (filter(abs)) {
      out.push(abs);
    }
  }
  return out;
}

const isProdCc = (f) =>
  f.endsWith('.cc') && !f.endsWith('_unittest.cc') &&
  !f.endsWith('_browsertest.cc') && !f.includes('/fake_') &&
  !path.basename(f).startsWith('fake_');

// --- 1 + 2: no capability dispatch switch / reinterpreting default ----------
// The task service resolves executors via executors_->Find(); it must never
// switch on a capability id string. Flag a switch/if chain that compares a
// capability/tool id to string literals in product production source.
for (const file of walk(productRoot, isProdCc)) {
  const text = fs.readFileSync(file, 'utf8');
  const rel = path.relative(repoRoot, file);
  // A chain of capability-id string comparisons is the smell we forbid.
  const idComparisons = text.match(
    /\.(value|id)\(\)\s*==\s*"[a-z]+\.[a-z.]+"/g,
  );
  if (idComparisons && idComparisons.length >= 3) {
    problems.push(
      `${rel}: looks like a capability-id dispatch chain ` +
        `(${idComparisons.length} id string comparisons); route through the ` +
        `CapabilityExecutorRegistry instead.`,
    );
  }
}

// --- 3: no placeholder production callbacks ---------------------------------
const PLACEHOLDER =
  /\b(TODO|FIXME|not implemented|notimplemented|placeholder|fake success|stub out|coming soon)\b/i;
for (const file of walk(productRoot, isProdCc).concat(
  walk(path.join(seoulRoot, 'browser/canvas'), isProdCc),
)) {
  const text = fs.readFileSync(file, 'utf8');
  const rel = path.relative(repoRoot, file);
  text.split('\n').forEach((line, i) => {
    // Allow the word in a longer descriptive comment only if it is not a
    // deferral marker; the regex above is already specific.
    if (PLACEHOLDER.test(line)) {
      problems.push(`${rel}:${i + 1}: placeholder marker in production source`);
    }
  });
}

// --- 4: no fixed workspace names in the shell -------------------------------
const shellRoot = path.join(seoulRoot, 'browser/shell');
for (const file of walk(shellRoot, isProdCc)) {
  const text = fs.readFileSync(file, 'utf8');
  const rel = path.relative(repoRoot, file);
  text.split('\n').forEach((line, i) => {
    if (/command\.name\s*=\s*"Workspace"/.test(line)) {
      problems.push(
        `${rel}:${i + 1}: workspace created with a fixed name; derive a real ` +
          `name from user input or a unique default.`,
      );
    }
    if (/\+\s*" Renamed"/.test(line)) {
      problems.push(
        `${rel}:${i + 1}: workspace renamed by appending a fixed suffix; ` +
          `use a real rename input.`,
      );
    }
  });
}

// --- 5: no empty browser-test bodies ----------------------------------------
for (const file of walk(seoulRoot, (f) => f.endsWith('_browsertest.cc'))) {
  const text = fs.readFileSync(file, 'utf8');
  const rel = path.relative(repoRoot, file);
  const empty = text.match(/IN_PROC_BROWSER_TEST_F\([^)]*\)\s*\{\s*\}/g);
  if (empty) {
    problems.push(
      `${rel}: ${empty.length} empty IN_PROC_BROWSER_TEST_F body/bodies`,
    );
  }
}

// --- 6: executor subclasses are registered ----------------------------------
// Collect CapabilityExecutor subclasses declared in product headers, then
// confirm each is constructed (std::make_unique<Name>) somewhere in product
// source. An executor no one registers is dead and misleading.
const executorNames = new Set();
for (const file of walk(productRoot, (f) => f.endsWith('.h'))) {
  const text = fs.readFileSync(file, 'utf8');
  for (const m of text.matchAll(
    /class\s+(\w+)\s*(?:final\s*)?:\s*public\s+CapabilityExecutor\b/g,
  )) {
    executorNames.add(m[1]);
  }
}
const productSource = walk(productRoot, (f) => f.endsWith('.cc'))
  .map((f) => fs.readFileSync(f, 'utf8'))
  .join('\n');
for (const name of executorNames) {
  const constructed = new RegExp(`make_unique<${name}>`).test(productSource);
  if (!constructed) {
    problems.push(
      `CapabilityExecutor subclass ${name} is never registered ` +
        `(no make_unique<${name}>); remove it or register it.`,
    );
  }
}

// --- 7: no active-window fallback for product turns -------------------------
for (const file of walk(productRoot, isProdCc).concat(
  walk(path.join(seoulRoot, 'browser/canvas'), isProdCc),
)) {
  const text = fs.readFileSync(file, 'utf8');
  const rel = path.relative(repoRoot, file);
  text.split('\n').forEach((line, i) => {
    if (/\bActiveWindow\s*\(/.test(line)) {
      problems.push(
        `${rel}:${i + 1}: product turns must resolve a ` +
          `WindowRuntimeBinding token, not an active-window fallback.`,
      );
    }
  });
}

// --- 8: primary Canvas voice uses the realtime agent ------------------------
const runtimeServicePath = path.join(
  productRoot,
  'browser/seoul_runtime_service.cc',
);
if (fs.existsSync(runtimeServicePath)) {
  const runtimeText = fs.readFileSync(runtimeServicePath, 'utf8');
  if (/AppleSpeechRecognizer|AppleTtsEngine/.test(runtimeText)) {
    problems.push(
      'SeoulRuntimeService wires platform speech directly; Canvas voice must ' +
        'mint realtime voice sessions through RealtimeVoiceAgent.',
    );
  }
  if (!/CreateRealtimeVoiceSession/.test(runtimeText)) {
    problems.push(
      'SeoulRuntimeService does not expose CreateRealtimeVoiceSession; Canvas ' +
        'voice has no production realtime session bridge.',
    );
  }
}
const previewWebViewPath = path.join(
  seoulRoot, 'browser/preview/seoul_preview_web_view.cc');
if (!fs.existsSync(previewWebViewPath)) {
  problems.push('Preview Chromium containment WebView source is missing.');
} else {
  const previewWebView = fs.readFileSync(previewWebViewPath, 'utf8');
  for (const containmentHook of [
    'AddNewContents',
    '*was_blocked = true',
    'std::move(callback).Run(false)',
    'ShouldSuppressDialogs',
    'CanEnterFullscreenModeForTab',
  ]) {
    if (!previewWebView.includes(containmentHook)) {
      problems.push(`Preview WebView is missing containment hook ${containmentHook}.`);
    }
  }
}
const canvasMojomPath = path.join(seoulRoot, 'browser/canvas/canvas.mojom');
if (fs.existsSync(canvasMojomPath)) {
  const mojomText = fs.readFileSync(canvasMojomPath, 'utf8');
  for (const method of [
    'CreateRealtimeVoiceSession',
    'SubmitRealtimeToolCall',
    'ProvideTaskInput',
  ]) {
    if (!mojomText.includes(method)) {
      problems.push(
        `Canvas Mojo is missing ${method}; voice cannot connect realtime ` +
          'speech to browser tasks.',
      );
    }
  }
}

// --- 9: execution permissions are exact-scope, not profile-wide ------------
const permissionHeaderPath = path.join(
  seoulRoot, 'browser/policy/agent_permission_service.h');
const permissionSourcePath = path.join(
  seoulRoot, 'browser/policy/agent_permission_service.cc');
const taskServicePath = path.join(productRoot, 'task_service.cc');
const runtimeServiceHeaderPath = path.join(
  productRoot, 'browser/seoul_runtime_service.h');
const genericCapabilitiesPath = path.join(
  seoulRoot, 'browser/connectors/generic_capabilities.cc');
for (const requiredPath of [permissionHeaderPath, permissionSourcePath]) {
  if (!fs.existsSync(requiredPath)) {
    problems.push(
      `${path.relative(repoRoot, requiredPath)}: origin-scoped permission ` +
        'boundary is missing.',
    );
  }
}
if (fs.existsSync(permissionHeaderPath)) {
  const permissionHeader = fs.readFileSync(permissionHeaderPath, 'utf8');
  for (const exactScopeField of [
    'LiveWindowKey window',
    'LiveTabKey tab',
    'frame_scope',
    'source_origin',
    'destination_origin',
    'service_scope',
  ]) {
    if (!permissionHeader.includes(exactScopeField)) {
      problems.push(
        `Agent permission request is missing exact scope field ${exactScopeField}.`,
      );
    }
  }
}
if (fs.existsSync(permissionSourcePath)) {
  const permissionSource = fs.readFileSync(permissionSourcePath, 'utf8');
  const comparesDestinationOriginExactly =
    permissionSource.includes(
      'request.destination_origin == stored.destination_origin',
    ) ||
    /SameOptionalOrigin\(\s*request\.destination_origin,\s*stored\.destination_origin\s*\)/s.test(
      permissionSource,
    );
  if (!comparesDestinationOriginExactly) {
    problems.push('Agent grants do not bind the exact destination origin.');
  }
  if (!permissionSource.includes('request.risk == RiskCategory::kExternalSideEffect')) {
    problems.push('External side effects are not forced through per-step approval.');
  }
}
if (fs.existsSync(taskServicePath)) {
  const taskService = fs.readFileSync(taskServicePath, 'utf8');
  for (const required of ['permissions_->Evaluate', 'GrantFirstUse',
                          'pending_permission_request', 'ProvideInput',
                          'OnReplanned']) {
    if (!taskService.includes(required)) {
      problems.push(
        `TaskService is missing permission execution hook "${required}".`,
      );
    }
  }
  if (/void TaskService::Replan[\s\S]{0,2500}BuildDeterministic/.test(
        taskService)) {
    problems.push(
      'TaskService::Replan hardcodes deterministic planning instead of preserving the configured route.',
    );
  }
  if (!/tasks_\[task_id\]\s*=\s*std::move\(task\);[\s\S]{0,500}NotifyUpdated\(task_id\)/
      .test(taskService) ||
      !taskService.includes('return TaskState::kPlanning;')) {
    problems.push(
      'TaskService does not publish an immediate planning state while reasoning is pending.',
    );
  }
  if (!taskService.includes('StateSummaries') ||
      !taskService.includes('EffectiveState')) {
    problems.push(
      'TaskService lacks a lean state-only projection for persistent browser chrome.',
    );
  }
}
if (fs.existsSync(genericCapabilitiesPath)) {
  const capabilities = fs.readFileSync(genericCapabilitiesPath, 'utf8');
  for (const pageRead of ['page.observe.text', 'page.extract.structured']) {
    const escaped = pageRead.replaceAll('.', '\\.');
    if (!(new RegExp(`${escaped}[\\s\\S]{0,1200}ApprovalPolicy::kFirstUsePerScope`))
        .test(capabilities)) {
      problems.push(`${pageRead} is not first-use-per-origin approval scoped.`);
    }
  }
  if (!/browser\.preview\.open[\s\S]{0,1500}ApprovalPolicy::kFirstUsePerScope/
      .test(capabilities) ||
      !/browser\.preview\.open[\s\S]{0,900}RequiredString\("tab_key"/
        .test(capabilities)) {
    problems.push(
      'browser.preview.open is not first-use scoped to an explicit parent tab.',
    );
  }
}
const planValidatorPath = path.join(
  seoulRoot, 'browser/tasks/plan_validator.cc');
if (fs.existsSync(planValidatorPath)) {
  const validator = fs.readFileSync(planValidatorPath, 'utf8');
  if (!validator.includes('if (!step.requires_approval)') ||
      !validator.includes('would bypass AgentPermissionService')) {
    problems.push(
      'Plan validation allows a generic gate to substitute for exact step-scoped permission.',
    );
  }
}
if (fs.existsSync(runtimeServicePath)) {
  const runtimeSource = fs.readFileSync(runtimeServicePath, 'utf8');
  for (const required of [
    'ResolveAgentPermissionRequest',
    'args.FindString("tab_key")',
    'descriptor.id.value() == "browser.preview.open"',
    'GetLastCommittedURL',
    'url::Origin::Create',
    'live_window_observation_.Observe',
    'agent_permissions_->RevokeTab',
    'agent_permissions_->RevokeWindow',
    'agent_permissions_->RevokeAll',
  ]) {
    if (!runtimeSource.includes(required)) {
      problems.push(
        `SeoulRuntimeService is missing exact-scope permission lifecycle hook ` +
          `"${required}".`,
      );
    }
  }
  if (!runtimeSource.includes('task_service_->StateSummaries()') ||
      /PublishShellTaskSummary[\s\S]{0,1800}task_service_->Snapshots\(\)/.test(
        runtimeSource)) {
    problems.push(
      'Shell task aggregation copies full task snapshots/receipts instead of state-only summaries.',
    );
  }
}
if (fs.existsSync(runtimeServiceHeaderPath)) {
  const runtimeHeader = fs.readFileSync(runtimeServiceHeaderPath, 'utf8');
  if (!runtimeHeader.includes('AgentPermissionService')) {
    problems.push(
      'SeoulRuntimeService does not own the agent permission service.',
    );
  }
  if (!runtimeHeader.includes('public LiveWindowStateObserver')) {
    problems.push(
      'SeoulRuntimeService does not observe live tab/window removal for grant revocation.',
    );
  }
}

// Library and Boards must be reachable through the same window-bound WebUI;
// an invisible service is not a product feature. These calls expose bounded
// metadata snapshots and typed mutations only.
const canvasHandlerPath = path.join(
  seoulRoot, 'browser/canvas/seoul_canvas_page_handler.cc');
if (fs.existsSync(canvasMojomPath)) {
  const mojomText = fs.readFileSync(canvasMojomPath, 'utf8');
  for (const method of [
    'GetLibrarySnapshot',
    'CreateBoard',
    'SetBoardArchived',
    'DeleteBoard',
    'GetStudioSnapshot',
    'UpsertEssential',
    'DeleteEssential',
  ]) {
    if (!mojomText.includes(method)) {
      problems.push(`Canvas Mojo is missing ${method}; Library/Boards are unreachable.`);
    }
  }
}
if (fs.existsSync(canvasHandlerPath) &&
    !fs.readFileSync(canvasHandlerPath, 'utf8').includes('runtime_->library()')) {
  problems.push('Canvas page handler does not connect its Library/Board Mojo methods.');
}
const libraryServicePath = path.join(
  seoulRoot, 'browser/library/library_service.cc');
if (fs.existsSync(libraryServicePath)) {
  const libraryService = fs.readFileSync(libraryServicePath, 'utf8');
  const archivedGuards = (
    libraryService.match(
      /return base::unexpected\(LibraryError::kBoardArchived\)/g,
    ) ?? []
  ).length;
  if (archivedGuards < 4) {
    problems.push(
      'Archived Boards are not read-only across rename/add/update/remove.',
    );
  }
  if (!libraryService.includes('revision_ =') ||
      !libraryService.includes('revision_ + 1')) {
    problems.push(
      'Library mutations do not advance a profile-owned snapshot revision.',
    );
  }
}
if (fs.existsSync(canvasHandlerPath)) {
  const handler = fs.readFileSync(canvasHandlerPath, 'utf8');
  if (!handler.includes('runtime_->library()->revision()')) {
    problems.push(
      'Canvas Library snapshots omit the authoritative revision; late replies can regress Board state.',
    );
  }
  for (const runtimeProjection of [
    'runtime_->providers()->Snapshot()',
    'runtime_->scenes()->List()',
    'runtime_->themes()->List()',
    'runtime_->site_layers()->List()',
  ]) {
    if (!handler.includes(runtimeProjection)) {
      problems.push(
        `Canvas Studio is missing live runtime projection ${runtimeProjection}.`,
      );
    }
  }
  for (const essentialMutation of [
    'runtime_->UpsertEssential',
    'runtime_->RemoveEssential',
    'OrganizationErrorToString',
  ]) {
    if (!handler.includes(essentialMutation)) {
      problems.push(
        `Canvas Studio Essentials are missing native hook ${essentialMutation}.`,
      );
    }
  }
  for (const forbiddenStudioField of [
    'local_endpoint',
    'last_error',
  ]) {
    const studioBody = handler.match(
      /StudioSnapshotJson\(\) const \{([\s\S]*?)\nvoid SeoulCanvasPageHandler::GetStudioSnapshot/,
    )?.[1] ?? '';
    if (studioBody.includes(`providers.${forbiddenStudioField}`)) {
      problems.push(
        `Canvas Studio exposes provider ${forbiddenStudioField}; keep endpoints ` +
          'and raw provider errors behind the browser boundary.',
      );
    }
  }
}

// Scene references must resolve against authoritative catalogs. Accepting any
// non-empty id would let a Scene validate with a phantom Theme/Site Layer and
// fail only after the user tried to activate it.
if (fs.existsSync(runtimeServicePath)) {
  const runtimeSource = fs.readFileSync(runtimeServicePath, 'utf8');
  for (const catalogCheck of [
    'registry->Exists(theme_id)',
    'registry->Exists(site_layer_id)',
    'themes_->TakePersistedState()',
    'themes_->RestorePersistedState(*themes)',
    'site_layers_->TakePersistedState()',
    'site_layers_->RestorePersistedState(*site_layers)',
    'runtime_.scenes().TakePersistedState()',
    'runtime_.scenes().RestorePersistedState(*scenes)',
  ]) {
    if (!runtimeSource.includes(catalogCheck)) {
      problems.push(
        `Appearance catalog integration is missing ${catalogCheck}.`,
      );
    }
  }
  if (/theme_exists[\s\S]{0,300}!theme_id\.empty\(\)/.test(runtimeSource)) {
    problems.push(
      'Scene Theme resolver accepts arbitrary non-empty ids instead of ThemeRegistry entries.',
    );
  }
}

// --- 9: capability arg names match between descriptor and executor ----------
// A capability descriptor declares its input fields (generic_capabilities.cc);
// the browser executor decodes those fields (browser_capabilities.cc). If the
// executor reads a field name the descriptor never declares, the capability is
// unrunnable (ValidatePlan rejects the unknown field, or the value is always
// missing). This catches that drift statically.
const declaredArgs = new Set();
const capsPath = path.join(
  seoulRoot,
  'browser/connectors/generic_capabilities.cc',
);
if (fs.existsSync(capsPath)) {
  const capsText = fs.readFileSync(capsPath, 'utf8');
  // Tolerate clang-format line wrapping between the helper call and its first
  // string argument (\s matches newlines in JS).
  for (const m of capsText.matchAll(
    /(?:Required|Optional)[A-Za-z]*\(\s*"([a-z_]+)"/g,
  )) {
    declaredArgs.add(m[1]);
  }
  for (const m of capsText.matchAll(/\.name\s*=\s*"([a-z_]+)"/g)) {
    declaredArgs.add(m[1]);
  }
}
const execPath = path.join(
  productRoot,
  'browser/browser_capabilities.cc',
);
if (fs.existsSync(execPath) && declaredArgs.size > 0) {
  const execText = fs.readFileSync(execPath, 'utf8');
  const execRel = path.relative(repoRoot, execPath);
  execText.split('\n').forEach((line, i) => {
    const m = line.match(/request\.args\.Find[A-Za-z]*\("([a-z_]+)"\)/);
    if (m && !declaredArgs.has(m[1])) {
      problems.push(
        `${execRel}:${i + 1}: executor reads arg "${m[1]}" that no capability ` +
          `descriptor declares; descriptor and executor arg names have drifted.`,
      );
    }
  });
}

// --- 10: task-to-surface is wired in production, not only in tests ----------
// A verified task result must reach the surface layer through a production
// caller of SurfaceService::CreateFromSemantic. If the only callers are test
// files (and its own definition), the product spine is broken: tasks would
// complete with no artifact. This locks in the TaskSurfaceBridge.
let productionCreateCallers = 0;
for (const file of walk(productRoot, isProdCc)) {
  // Exclude the method's own definition site.
  if (path.basename(file) === 'surface_service.cc') {
    continue;
  }
  const text = fs.readFileSync(file, 'utf8');
  if (/->CreateFromSemantic\(|\.CreateFromSemantic\(/.test(text)) {
    productionCreateCallers++;
  }
}
if (productionCreateCallers === 0) {
  problems.push(
    'SurfaceService::CreateFromSemantic has no production caller; a completed ' +
      'task would produce no surface. Wire it through a production observer ' +
      '(TaskSurfaceBridge), not only tests.',
  );
}

// --- 11: Canvas is a real window-bound side-panel entry ---------------------
// Registering only a WebUIConfig or only reserving an entry id leaves Canvas
// unreachable. Lock together the pinned Chromium registration, the top-chrome
// controller contract, and the Shell command that opens the bound window's
// exact entry.
const integrationPatchPath = path.join(
  repoRoot,
  'native/patches/chromium/0001-seoul-native-core.patch',
);
const canvasUiHeaderPath = path.join(
  seoulRoot,
  'browser/canvas/seoul_canvas_ui.h',
);
const shellServicePath = path.join(
  seoulRoot,
  'browser/shell/shell_service.cc',
);
const launcherPath = path.join(
  seoulRoot,
  'browser/shell/command_launcher_catalog.cc',
);
if (fs.existsSync(integrationPatchPath)) {
  const patchText = fs.readFileSync(integrationPatchPath, 'utf8');
  for (const required of [
    'CreateSeoulCanvasWebView',
    'SidePanelEntry::Id::kSeoulCanvas',
    'window_registry->Register(std::move(entry))',
    'shell->RegisterVerticalRegion(key, this,',
  ]) {
    if (!patchText.includes(required)) {
      problems.push(
        `Chromium integration patch is missing "${required}"; Canvas would ` +
          'not be a registered window-bound side-panel entry.',
      );
    }
  }
}
if (fs.existsSync(canvasUiHeaderPath) &&
    !fs.readFileSync(canvasUiHeaderPath, 'utf8')
      .includes('public TopChromeWebUIController')) {
  problems.push(
    'SeoulCanvasUI is not a TopChromeWebUIController; it cannot be hosted by ' +
      'the typed side-panel WebUI wrapper.',
  );
}
if (fs.existsSync(shellServicePath) &&
    !fs.readFileSync(shellServicePath, 'utf8')
      .includes('side_panel->Show(SidePanelEntryId::kSeoulCanvas)')) {
  problems.push(
    'ShellService does not open the kSeoulCanvas entry for its bound window.',
  );
}
if (fs.existsSync(launcherPath) &&
    !fs.readFileSync(launcherPath, 'utf8').includes('"open_canvas"')) {
  problems.push('The Shell command launcher has no reachable Canvas command.');
}

// The launcher must be a real local search surface, not a static menu backed
// by an unused filter or a string-id dispatch chain.
const launcherViewPath = path.join(
  seoulRoot, 'browser/shell/views/seoul_command_launcher_view.cc');
if (fs.existsSync(launcherViewPath)) {
  const launcherView = fs.readFileSync(launcherViewPath, 'utf8');
  for (const required of [
    'views::TextfieldController',
    'ContentsChanged',
    'CommandLauncherCatalog::Filter',
    'RunUtilityAction(entry.action)',
    'VKEY_RETURN',
    'VKEY_DOWN',
    'std::ranges::find_if',
    'result_status_',
    'SetLiveRegionContainer',
    'gfx::Animation::PrefersReducedMotion()',
  ]) {
    if (!launcherView.includes(required)) {
      problems.push(`Shell command launcher is missing searchable UI hook "${required}".`);
    }
  }
  if (/entry\.id\s*==/.test(launcherView)) {
    problems.push(
      'Shell command launcher dispatches string ids; use typed ShellUtilityAction.',
    );
  }
}

const shellHeaderViewPath = path.join(
  seoulRoot, 'browser/shell/views/seoul_shell_header_view.cc');
const shellFooterViewPath = path.join(
  seoulRoot, 'browser/shell/views/seoul_shell_footer_view.cc');
if (fs.existsSync(shellHeaderViewPath) && fs.existsSync(shellFooterViewPath)) {
  const compactShell = fs.readFileSync(shellHeaderViewPath, 'utf8') +
      fs.readFileSync(shellFooterViewPath, 'utf8');
  const normalizedCompactShell = compactShell.replace(/\s+/g, ' ');
  for (const required of [
    'SetPresentationCollapsed',
    'presentation_collapsed_',
    'Orientation::kVertical',
    'workspace.icon',
    'SetVisible(!collapsed)',
    'ShellUtilityAction::kOpenBoost',
    'OnSplitPressed',
    'ToggleCompactMode',
    'essentials_container_->SetVisible(!snapshot.essentials.empty())',
    'SetTooltipText',
    'LiveRegionStatus::kPolite',
    'button_row_layout_->SetOrientation',
    'OnEssentialsOverflowPressed',
    'overflow_essentials_',
    'workspace_accessible_name',
  ]) {
    if (!compactShell.includes(required)) {
      problems.push(`Native project shell is missing compact/project hook "${required}".`);
    }
  }
  for (const forbidden of [
    'new_tab_button_->SetText(collapsed',
    'split_button_->SetText(collapsed',
    'task_button_->SetVisible(collapsed',
  ]) {
    if (compactShell.includes(forbidden)) {
      problems.push(
        `Collapsed native shell still renders the legacy glyph rail "${forbidden}".`,
      );
    }
  }
  for (const acceleratorHook of [
    'AddAccelerator(ui::Accelerator(',
    'EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN',
    'AcceleratorPressed',
    'OnCommandLauncherPressed();',
  ]) {
    if (!normalizedCompactShell.includes(acceleratorHook)) {
      problems.push(`Window-scoped launcher accelerator is missing hook "${acceleratorHook}".`);
    }
  }
  for (const taskDeckHook of [
    'task_button_',
    'TaskButtonLabel',
    'TaskAccessibleName',
    'ShellUtilityAction::kOpenTaskDeck',
  ]) {
    if (!compactShell.includes(taskDeckHook)) {
      problems.push(`Persistent shell Task Deck is missing hook "${taskDeckHook}".`);
    }
  }
}

const liveWindowStatePath = path.join(
  seoulRoot, 'browser/lifecycle/live_window_state.cc');
const tabStripBridgePath = path.join(
  seoulRoot, 'browser/lifecycle/tab_strip_bridge.cc');
const shellViewModelPath = path.join(
  seoulRoot, 'browser/shell/shell_view_model.cc');
const shellControllerPath = path.join(
  seoulRoot, 'browser/shell/shell_controller.cc');
const splitChooserPath = path.join(
  seoulRoot, 'browser/shell/views/seoul_split_chooser_view.cc');
if (fs.existsSync(liveWindowStatePath) && fs.existsSync(tabStripBridgePath) &&
    fs.existsSync(shellViewModelPath) && fs.existsSync(shellControllerPath)) {
  const liveWindowState = fs.readFileSync(liveWindowStatePath, 'utf8');
  const shellViewModel = fs.readFileSync(shellViewModelPath, 'utf8');
  const shellController = fs.readFileSync(shellControllerPath, 'utf8');
  const tabStripBridge = fs.readFileSync(tabStripBridgePath, 'utf8');
  for (const required of [
    'kMaxLiveTabTitleLength',
    'origin.Serialize()',
  ]) {
    if (!liveWindowState.includes(required)) {
      problems.push(`Live tab presentation metadata is missing minimized hook "${required}".`);
    }
  }
  for (const required of ['essential_origin.Serialize()', 'item.live_tab']) {
    if (!shellViewModel.includes(required)) {
      problems.push(`Essential live association is missing hook "${required}".`);
    }
  }
  if (!shellController.includes('command.kind = CommandKind::kActivateTab')) {
    problems.push('Opening an Essential never activates its existing live tab.');
  }
  for (const required of [
    'context.other_live_windows',
    'item.live_window',
  ]) {
    if (!shellViewModel.includes(required)) {
      problems.push(`Cross-window Essential association is missing hook "${required}".`);
    }
  }
  for (const required of [
    'focus_window_callback_.Run(target_window)',
    'command.window = shell_item->live_window',
  ]) {
    if (!shellController.includes(required)) {
      problems.push(`Cross-window Essential activation is missing hook "${required}".`);
    }
  }
  if (/OnLiveWindowSnapshotChanged[\s\S]{0,400}snapshot\.window\s*!=\s*window_[\s\S]{0,120}return;/.test(
        shellController)) {
    problems.push(
      'ShellController ignores other-window snapshot changes and can retain stale Essential routing.',
    );
  }
  const tabChangeHandler = tabStripBridge.match(
    /void TabStripBridge::OnTabChangedAt[\s\S]*?(?=\nvoid TabStripBridge::OnTabPinnedStateChanged)/,
  )?.[0] ?? '';
  if (!tabChangeHandler.includes('PublishLiveSnapshot();') ||
      !/change_type\s*!=\s*TabChangeType::kAll\s*&&\s*change_type\s*!=\s*TabChangeType::kLoadingOnly/
        .test(tabChangeHandler)) {
    problems.push('Live tab origin/title metadata is not refreshed after navigation changes.');
  }
}
const shellServiceSourcePath = path.join(
  seoulRoot, 'browser/shell/shell_service.cc');
if (fs.existsSync(shellServiceSourcePath)) {
  const shellService = fs.readFileSync(shellServiceSourcePath, 'utf8');
  for (const required of [
    'ProfileBrowserCollection::GetForProfile',
    'FindBrowserWithID',
    'target.session_id()',
    'browser->GetWindow()->Activate()',
  ]) {
    if (!shellService.includes(required)) {
      problems.push(`Exact-window Essential focus is missing hook "${required}".`);
    }
  }
  for (const exactCanvasHook of [
    'browser_id.id() != window.session_id()',
    'bound_window.session_id()',
    'browser->GetFeatures().side_panel_ui()',
  ]) {
    if (!shellService.includes(exactCanvasHook)) {
      problems.push(
        `Exact-window Canvas launcher is missing hook "${exactCanvasHook}".`,
      );
    }
  }
  if (shellService.includes('base::Unretained(browser_window)')) {
    problems.push(
      'Shell Canvas launcher retains a raw browser-window callback across close.',
    );
  }
}
const shellRegionHostPath = path.join(
  seoulRoot, 'browser/shell/views/seoul_shell_region_host.cc');
if (fs.existsSync(shellRegionHostPath) &&
    fs.readFileSync(shellRegionHostPath, 'utf8').includes('value_or(0)')) {
  problems.push(
    'Shell region host moves the footer to index zero when its anchor is absent.',
  );
}
if (fs.existsSync(splitChooserPath) && fs.existsSync(shellControllerPath) &&
    fs.existsSync(shellViewModelPath)) {
  const splitChooser = fs.readFileSync(splitChooserPath, 'utf8');
  const shellController = fs.readFileSync(shellControllerPath, 'utf8');
  const shellViewModel = fs.readFileSync(shellViewModelPath, 'utf8');
  for (const required of [
    'controller->SplitCandidates()',
    'CreateSplitWithPartner',
    'CandidateLabel',
  ]) {
    if (!splitChooser.includes(required)) {
      problems.push(`Split chooser is missing explicit-partner UI hook "${required}".`);
    }
  }
  if (!splitChooser.includes('ui::MENU_SOURCE_KEYBOARD') &&
      !splitChooser.includes('ui::mojom::MenuSourceType::kKeyboard')) {
    problems.push(
      'Split chooser is missing an explicit keyboard menu-source hook.',
    );
  }
  if (!shellViewModel.includes('BuildSplitCandidates') ||
      !shellViewModel.includes('upstream_split_token')) {
    problems.push('Split candidates are not derived from live unsplit tabs.');
  }
  if (!/candidates\.size\(\)\s*!=\s*1u/.test(shellController)) {
    problems.push(
      'CreateSplitFromActive can silently choose among multiple partner tabs.',
    );
  }
}

if (fs.existsSync(runtimeServicePath)) {
  const runtimeSource = fs.readFileSync(runtimeServicePath, 'utf8');
  for (const required of [
    'task_service_->AddObserver(this)',
    'PublishShellTaskSummary',
    'shell->UpdateTaskSummary',
    'task_service_->RemoveObserver(this)',
    'shell->ClearTaskSummaries()',
  ]) {
    if (!runtimeSource.includes(required)) {
      problems.push(`Window-scoped shell task bridge is missing hook "${required}".`);
    }
  }
}

// --- 12: shipping Canvas is Lit and covers the visual catalog ---------------
// The browser renderer is a security/product boundary, not a design-lab DOM
// fixture. Keep it on Chromium's audited Lit stack, forbid raw DOM/HTML sinks,
// and make catalog growth fail CI until the new visual type is deliberately
// implemented.
const canvasResources = path.join(seoulRoot, 'browser/canvas/resources');
const canvasEntryPath = path.join(canvasResources, 'canvas.ts');
const canvasTemplatePath = path.join(canvasResources, 'canvas.html.ts');
const canvasVisualsPath = path.join(canvasResources, 'canvas_visualizations.ts');
const canvasBuildPath = path.join(canvasResources, 'BUILD.gn');
for (const requiredPath of [
  canvasEntryPath,
  canvasTemplatePath,
  canvasVisualsPath,
  canvasBuildPath,
]) {
  if (!fs.existsSync(requiredPath)) {
    problems.push(
      `${path.relative(repoRoot, requiredPath)}: required shipping Canvas Lit ` +
        'resource is missing.',
    );
  }
}
if (fs.existsSync(canvasEntryPath)) {
  const canvasEntry = fs.readFileSync(canvasEntryPath, 'utf8');
  for (const essentialUiHook of [
    'openEssentialEditor_',
    'saveEssential_',
    'upsertEssential(',
    'deleteEssential(',
    'pageContext_.origin',
  ]) {
    if (!canvasEntry.includes(essentialUiHook)) {
      problems.push(
        `Canvas Studio Essentials are missing UI hook ${essentialUiHook}.`,
      );
    }
  }
}
if (fs.existsSync(canvasEntryPath)) {
  const canvasEntry = fs.readFileSync(canvasEntryPath, 'utf8');
  if (!/extends\s+CrLitElement/.test(canvasEntry)) {
    problems.push('Shipping Canvas app does not extend CrLitElement.');
  }
  for (const sink of ['innerHTML', 'outerHTML', 'insertAdjacentHTML',
                      'replaceChildren(', 'document.createElement(']) {
    if (canvasEntry.includes(sink)) {
      problems.push(
        `Shipping Canvas uses imperative/raw DOM sink "${sink}"; render the ` +
          'validated catalog through Lit templates.',
      );
    }
  }
  // SurfaceToValue serializes component action references as `actions`.
  if (!/node\.actions\?/.test(canvasEntry) || /node\.action_ids/.test(canvasEntry)) {
    problems.push(
      'Shipping Canvas action binding does not consume the canonical `actions` field.',
    );
  }
  for (const view of [
    "'library'", "'boards'", "'studio'", 'getLibrarySnapshot',
    'getStudioSnapshot', 'renderStudio_',
  ]) {
    if (!canvasEntry.includes(view)) {
      problems.push(`Shipping Canvas is missing reachable ${view} Library/Board UI wiring.`);
    }
  }
  for (const boardEditorHook of [
    'olderRevision(',
    'flushBoardKeyboard_',
    'enqueueBoardElementCommit_',
    'boardPendingLayouts_',
    'event.altKey',
    'aria-keyshortcuts',
    'data-history-action="undo"',
    'data-history-action="redo"',
    'role="region"',
  ]) {
    if (!canvasEntry.includes(boardEditorHook)) {
      problems.push(
        `Shipping Board editor is missing production interaction hook ${boardEditorHook}.`,
      );
    }
  }
  const canvasRendererText = canvasEntry +
      (fs.existsSync(canvasTemplatePath) ?
        fs.readFileSync(canvasTemplatePath, 'utf8') : '');
  for (const inputHook of ['pending_user_input', 'provideTaskInput']) {
    if (!canvasRendererText.includes(inputHook)) {
      problems.push(`Shipping Canvas is missing typed task-input hook ${inputHook}.`);
    }
  }
}
if (fs.existsSync(canvasTemplatePath) &&
    !fs.readFileSync(canvasTemplatePath, 'utf8').includes('html`')) {
  problems.push('Shipping Canvas checked-in html.ts does not contain a Lit template.');
}
if (fs.existsSync(canvasBuildPath)) {
  const canvasBuild = fs.readFileSync(canvasBuildPath, 'utf8');
  for (const required of [
    'canvas.html.ts',
    'canvas_visualizations.ts',
    '//third_party/lit/v3_0:build_ts',
  ]) {
    if (!canvasBuild.includes(required)) {
      problems.push(`Canvas build is missing Lit resource/dependency "${required}".`);
    }
  }
}
const catalogPath = path.join(repoRoot, 'protocol/component-catalog.json');
if (fs.existsSync(catalogPath) && fs.existsSync(canvasEntryPath)) {
  const catalog = JSON.parse(fs.readFileSync(catalogPath, 'utf8')).components;
  const acceptedVisualTypes = Object.entries(catalog)
    .filter(([, metadata]) =>
      metadata.category === 'chart' ||
        (metadata.category === 'map' && metadata.input === false))
    .map(([type]) => type);
  const canvasEntry = fs.readFileSync(canvasEntryPath, 'utf8');
  for (const type of acceptedVisualTypes) {
    if (!canvasEntry.includes(`'${type}'`)) {
      problems.push(
        `Accepted visual component "${type}" is not handled by shipping Canvas.`,
      );
    }
  }
}

// --- 13: Preview lifecycle is ephemeral and promotion is explicit ----------
const previewManagerPath = path.join(
  seoulRoot, 'browser/preview/preview_manager.cc');
const previewTypesPath = path.join(
  seoulRoot, 'browser/preview/preview_types.h');
for (const requiredPath of [previewManagerPath, previewTypesPath]) {
  if (!fs.existsSync(requiredPath)) {
    problems.push(
      `${path.relative(repoRoot, requiredPath)}: Preview lifecycle source is missing.`,
    );
  }
}
if (fs.existsSync(previewManagerPath)) {
  const previewManager = fs.readFileSync(previewManagerPath, 'utf8');
  for (const invariant of [
    'MarkLoading',
    'BeginPromotion',
    'CommitPromotion',
    'AbortPromotion',
    'PreviewState::kPromoting',
    'preview_by_window_',
  ]) {
    if (!previewManager.includes(invariant)) {
      problems.push(`Preview lifecycle is missing invariant ${invariant}.`);
    }
  }
  const idAdmission = previewManager.indexOf('record.id = id_generator_.Run()');
  const replacementRemoval = previewManager.indexOf('Remove(existing->id)');
  if (idAdmission < 0 || replacementRemoval < 0 ||
      idAdmission > replacementRemoval) {
    problems.push(
      'Preview replacement destroys the existing preview before the new id is admitted.',
    );
  }
  if (/TakePersistedState|RestorePersistedState/.test(previewManager)) {
    problems.push('Preview state must remain ephemeral across restart.');
  }
}
const lifecycleCoordinatorPath = path.join(
  seoulRoot, 'browser/lifecycle/lifecycle_coordinator.cc');
if (fs.existsSync(lifecycleCoordinatorPath)) {
  const coordinator = fs.readFileSync(lifecycleCoordinatorPath, 'utf8');
  for (const insertionHandshake of [
    'ExpectTabInsertion',
    'expected_insertions_.contains(tab)',
    'expected->second.window == event.window',
    'model_->AddTabMembership(ws, tab_key, insertion_role)',
    'ExpireExpectedInsertionsForWindow',
  ]) {
    if (!coordinator.includes(insertionHandshake)) {
      problems.push(
        `Preview promotion insertion handshake is missing ${insertionHandshake}.`,
      );
    }
  }
}
if (fs.existsSync(runtimeServicePath)) {
  const runtimeSource = fs.readFileSync(runtimeServicePath, 'utf8');
  for (const runtimePreviewHook of [
    'std::make_unique<PreviewManager>',
    'std::make_unique<PreviewHostService>',
    'preview_host_service_->DismissForParent(previous)',
    'preview_host_service_->DismissForWindow(window)',
    'preview_host_service_->Shutdown()',
    'preview_manager_.reset()',
  ]) {
    if (!runtimeSource.includes(runtimePreviewHook)) {
      problems.push(
        `Profile runtime is missing Preview lifecycle hook ${runtimePreviewHook}.`,
      );
    }
  }
}
const previewHostPath = path.join(
  seoulRoot, 'browser/preview/preview_host_service.cc');
if (!fs.existsSync(previewHostPath)) {
  problems.push('Preview Chromium bubble/transfer service source is missing.');
} else {
  const previewHost = fs.readFileSync(previewHostPath, 'utf8');
  for (const hostInvariant of [
    'content::WebContents::Create',
    'SeoulPreviewBubbleView',
    'ExpectTabInsertion(record_.window, promoted_tab',
    'InsertWebContentsAt',
    'AddToNewSplit',
    'CommitPromotion',
    'RestoreParentFocus',
    'CloseSuperseded',
    'SetLiveRegionContainer',
    'SetPromotionActionsEnabled(false)',
    'SetPreviewStatus(u"Ready · not saved as a tab", true)',
    'gfx::Animation::PrefersReducedMotion()',
    'ResolveBrowser(profile_, record_.window)',
    'OpenFromLink',
    'collection->FindBrowserWithTab(parent_contents)',
    'parent_contents->GetBrowserContext() != profile_',
  ]) {
    if (!previewHost.includes(hostInvariant)) {
      problems.push(`Preview host is missing transfer invariant ${hostInvariant}.`);
    }
  }
}
if (fs.existsSync(integrationPatchPath)) {
  const patch = fs.readFileSync(integrationPatchPath, 'utf8');
  for (const linkEntryInvariant of [
    'IDC_CONTENT_CONTEXT_OPENLINKPREVIEW',
    'IDS_CONTENT_CONTEXT_OPENLINKPREVIEW',
    'params_.link_url.SchemeIsHTTPOrHTTPS()',
    'IsOpenLinkAllowedByDlp(params_.link_url)',
    'preview->OpenFromLink(source_web_contents_, params_.link_url)',
    'IDC_CONTENT_CONTEXT_OPENLINKPREVIEW, 165',
  ]) {
    if (!patch.includes(linkEntryInvariant)) {
      problems.push(
        `Preview link context-menu entry is missing ${linkEntryInvariant}.`,
      );
    }
  }
}
if (!productSource.includes('make_unique<PreviewOpenExecutor>') ||
    !productSource.includes('browser.preview.open')) {
  problems.push('Typed browser.preview.open executor is not registered.');
}
const runtimeBrowserTestPath = path.join(
  productRoot, 'browser/seoul_runtime_browsertest.cc');
if (!fs.existsSync(runtimeBrowserTestPath) ||
    !fs.readFileSync(runtimeBrowserTestPath, 'utf8')
      .includes('PreviewOpensOutsideTabStripAndDismissesCleanly')) {
  problems.push(
    'Preview host has no real-browser test for non-tab open and clean dismissal.',
  );
}

// --- 14: native page values and sensitive fields stay outside the model ----
// The extension harness is not the product security boundary. Keep the same
// fail-closed rule in the Chromium-owned PageAgent: no current form values in
// semantic observations, standards-backed credential/payment classification,
// and value-action refusal before an AX mutation reaches the renderer.
const pageFieldSafetyPath = path.join(
  productRoot, 'page_field_safety.cc');
const pageFieldSafetyTestPath = path.join(
  productRoot, 'page_field_safety_unittest.cc');
const productCoreBuildPath = path.join(productRoot, 'BUILD.gn');
const pageAgentPath = path.join(productRoot, 'browser/page_agent.cc');
const browserCapabilitiesPath = path.join(
  productRoot, 'browser/browser_capabilities.cc');
for (const requiredPath of [pageFieldSafetyPath, pageFieldSafetyTestPath,
  pageAgentPath, browserCapabilitiesPath]) {
  if (!fs.existsSync(requiredPath)) {
    problems.push(
      `${path.relative(repoRoot, requiredPath)}: native sensitive-field ` +
        'boundary source is missing.',
    );
  }
}
if (fs.existsSync(productCoreBuildPath)) {
  const productBuild = fs.readFileSync(productCoreBuildPath, 'utf8');
  for (const source of [
    'page_field_safety.cc',
    'page_field_safety.h',
    'page_field_safety_unittest.cc',
  ]) {
    if (!productBuild.includes(source)) {
      problems.push(`Product GN graph does not compile ${source}.`);
    }
  }
}
if (fs.existsSync(pageFieldSafetyPath)) {
  const fieldSafety = fs.readFileSync(pageFieldSafetyPath, 'utf8');
  for (const signal of [
    'descriptor.protected_state',
    'current-password',
    'one-time-code',
    'token.starts_with("cc-")',
    'transaction-amount',
  ]) {
    if (!fieldSafety.includes(signal)) {
      problems.push(`Page field safety policy is missing signal ${signal}.`);
    }
  }
}
if (fs.existsSync(pageAgentPath)) {
  const pageAgent = fs.readFileSync(pageAgentPath, 'utf8');
  for (const boundary of [
    'snapshot_mode.set_mode(ui::AXMode::kHTML, true)',
    'node.IsPasswordField()',
    'ClassifyPageField(field)',
    'PageActionStatus::kSensitiveField',
  ]) {
    if (!pageAgent.includes(boundary)) {
      problems.push(`Native PageAgent is missing safety boundary ${boundary}.`);
    }
  }
}
if (fs.existsSync(browserCapabilitiesPath)) {
  const capabilities = fs.readFileSync(browserCapabilitiesPath, 'utf8');
  if (/row\.Set\("value"|Field\("value"/.test(capabilities)) {
    problems.push(
      'Page observation projects current form values into model-visible semantics.',
    );
  }
  for (const projection of ['agent_writable', 'sensitivity']) {
    if (!capabilities.includes(projection)) {
      problems.push(`Page observation omits safe field metadata ${projection}.`);
    }
  }
}
if (!fs.existsSync(runtimeBrowserTestPath) ||
    !fs.readFileSync(runtimeBrowserTestPath, 'utf8')
      .includes('SensitiveFieldsAreRedactedAndNotModelWritable')) {
  problems.push(
    'Native sensitive fields have no browser-level non-mutation regression test.',
  );
}

// --- 15: Live Collections remain executable, observable, and tested ---------
// A persisted card without the coordinator, Canvas controls, or a real-browser
// execution test is a misleading placeholder. Keep the whole vertical slice
// wired through typed, read-only capability execution.
const liveCollectionHeaderPath = path.join(
  productRoot, 'live_collection_coordinator.h');
const liveCollectionSourcePath = path.join(
  productRoot, 'live_collection_coordinator.cc');
const liveCollectionTestPath = path.join(
  productRoot, 'live_collection_coordinator_unittest.cc');
const canvasHandlerHeaderPath = path.join(
  seoulRoot, 'browser/canvas/seoul_canvas_page_handler.h');
for (const requiredPath of [
  liveCollectionHeaderPath,
  liveCollectionSourcePath,
  liveCollectionTestPath,
]) {
  if (!fs.existsSync(requiredPath)) {
    problems.push(
      `${path.relative(repoRoot, requiredPath)}: executable Live Collections ` +
        'runtime source is missing.',
    );
  }
}
if (fs.existsSync(productCoreBuildPath)) {
  const productBuild = fs.readFileSync(productCoreBuildPath, 'utf8');
  for (const source of [
    'live_collection_coordinator.cc',
    'live_collection_coordinator.h',
    'live_collection_coordinator_unittest.cc',
  ]) {
    if (!productBuild.includes(source)) {
      problems.push(`Product GN graph does not compile ${source}.`);
    }
  }
}
if (fs.existsSync(runtimeServicePath)) {
  const runtimeSource = fs.readFileSync(runtimeServicePath, 'utf8');
  for (const hook of [
    'std::make_unique<LiveCollectionCoordinator>',
    'RunLiveCollectionMaintenance',
    'live_collection_coordinator_->CancelForWindow',
    'live_collection_coordinator_->Shutdown',
  ]) {
    if (!runtimeSource.includes(hook)) {
      problems.push(`Live Collections runtime is missing lifecycle hook ${hook}.`);
    }
  }
}
if (fs.existsSync(canvasMojomPath)) {
  const mojom = fs.readFileSync(canvasMojomPath, 'utf8');
  for (const method of [
    'UpsertLiveCollection',
    'SetLiveCollectionEnabled',
    'RefreshLiveCollection',
    'DeleteLiveCollection',
    'OpenLiveCollectionItem',
    'PushLibrarySnapshot',
  ]) {
    if (!mojom.includes(method)) {
      problems.push(`Canvas Mojo is missing Live Collections method ${method}.`);
    }
  }
}
if (fs.existsSync(canvasHandlerPath)) {
  const handler = fs.readFileSync(canvasHandlerPath, 'utf8');
  for (const hook of [
    'OnLibraryChanged',
    'runtime_->live_collections()->Refresh',
  ]) {
    if (!handler.includes(hook)) {
      problems.push(`Canvas Live Collections bridge is missing hook ${hook}.`);
    }
  }
}
if (!fs.existsSync(canvasHandlerHeaderPath) ||
    !fs.readFileSync(canvasHandlerHeaderPath, 'utf8')
      .includes('public LibraryServiceObserver')) {
  problems.push(
    'Canvas PageHandler does not observe shared Library mutations.',
  );
}
if (!fs.existsSync(runtimeBrowserTestPath) ||
    !fs.readFileSync(runtimeBrowserTestPath, 'utf8')
      .includes('CanvasLiveCollectionsExecutePersistAndManageRealSource')) {
  problems.push(
    'Live Collections have no real-browser execution and persistence test.',
  );
}

// M149 builds the sessions implementation and SessionID from the root BUILD;
// the historical /content and /core labels are header directories only.
for (const file of walk(seoulRoot, (f) => f.endsWith('BUILD.gn'))) {
  const build = fs.readFileSync(file, 'utf8');
  if (/"\/\/components\/sessions\/(?:content|core)"/.test(build)) {
    problems.push(
      `${path.relative(repoRoot, file)}: references a nonexistent M149 ` +
        'components/sessions subdirectory target.',
    );
  }
}

if (problems.length) {
  console.error('product-architecture: FAILED');
  for (const p of problems) console.error('  - ' + p);
  process.exit(1);
}
console.log(
  `product-architecture: OK (dispatch-registry, fail-closed, no placeholders, ` +
    `real workspace names, no empty browser tests, window-bound Canvas, ` +
    `realtime Canvas voice wired, ${executorNames.size} executors all ` +
    `registered, capability arg names consistent, task-to-surface bridge ` +
    `wired, Canvas side-panel entry reachable, Lit visual catalog complete, ` +
    `Library/Boards and Studio provider routes reachable, navigation-fresh ` +
    `live metadata, authoritative Scene references, ` +
    `executable observable Live Collections, ` +
    `exact-scope agent permissions enforced, ` +
    `typed task input replans, searchable typed-action launcher, explicit ` +
    `split chooser, persistent shell Task Deck status, exact cross-window ` +
    `Essentials, explicit ephemeral Preview lifecycle, retained-insertion ` +
    `handshake, and native sensitive-field refusal)`,
);
