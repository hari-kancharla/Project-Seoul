#!/usr/bin/env node
// Canonical-protocol gate. Fails when:
//   1. protocol/ts/types.ts drifts from the schemas (regeneration check);
//   2. any schema enum diverges from the native C++ wire names, extracted
//      from the tracked native sources (no build required);
//   3. any schema file fails to parse or contains an unresolvable $ref.
// This is the cross-language drift gate: the schemas cannot silently fall
// behind the native model, and the generated TypeScript cannot fall behind
// the schemas.
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const problems = [];

function read(rel) {
  return readFileSync(join(repoRoot, rel), 'utf8');
}

function schema(rel) {
  return JSON.parse(read(join('protocol', rel)));
}

function compareSets(label, schemaValues, nativeValues) {
  const fromSchema = new Set(schemaValues);
  const fromNative = new Set(nativeValues);
  for (const value of fromNative) {
    if (!fromSchema.has(value)) {
      problems.push(`${label}: native wire name "${value}" missing from schema enum`);
    }
  }
  for (const value of fromSchema) {
    if (!fromNative.has(value)) {
      problems.push(`${label}: schema enum value "${value}" has no native wire name`);
    }
  }
  if (schemaValues.length !== fromSchema.size) {
    problems.push(`${label}: schema enum contains duplicates`);
  }
}

// Extracts the string returned for each case of one ToString function body.
function switchStrings(source, functionName) {
  const start = source.indexOf(`const char* ${functionName}(`);
  if (start === -1) {
    problems.push(`native extraction: ${functionName} not found`);
    return [];
  }
  const openBrace = source.indexOf('{', start);
  let depth = 0;
  let end = openBrace;
  for (let i = openBrace; i < source.length; i++) {
    if (source[i] === '{') depth++;
    if (source[i] === '}') depth--;
    if (depth === 0) {
      end = i;
      break;
    }
  }
  const body = source.slice(openBrace, end);
  const values = [...body.matchAll(/return "([a-z_0-9]*)";/g)].map((m) => m[1]);
  // The final return after the switch is the fallback; every enum value also
  // appears as a case return, so a plain de-dup yields the wire-name set.
  return [...new Set(values)];
}

// ---- 1. Generated types in sync -------------------------------------------
try {
  execFileSync(process.execPath, [join(repoRoot, 'scripts', 'generate-protocol-types.mjs'), '--check'], {
    stdio: ['ignore', 'ignore', 'pipe'],
  });
} catch (error) {
  problems.push(`generated types stale: ${String(error.stderr ?? error.message).trim()}`);
}

// ---- 1b. Component catalog in sync with the native sources ----------------
try {
  execFileSync(process.execPath, [join(repoRoot, 'scripts', 'generate-component-catalog.mjs'), '--check'], {
    stdio: ['ignore', 'ignore', 'pipe'],
  });
} catch (error) {
  problems.push(`component catalog stale: ${String(error.stderr ?? error.message).trim()}`);
}

// ---- 2. Schema parse + $ref integrity via the shared validator ------------
const files = [
  'semantic-result.schema.json',
  'adaptive-surface.schema.json',
  'surface-patch.schema.json',
  'component-event.schema.json',
  'task-snapshot.schema.json',
  'capability-descriptor.schema.json',
  'context-map.schema.json',
];
const registry = {};
for (const file of files) {
  try {
    registry[file] = schema(file);
  } catch (error) {
    problems.push(`${file}: ${error.message}`);
  }
}
const { validate } = await import(join(repoRoot, 'protocol', 'ts', 'validate.mjs'));
// Validating one representative fixture per schema exercises every $ref path.
const representative = {
  'semantic-result.schema.json': 'fixtures/semantic/time-series.json',
  'adaptive-surface.schema.json': 'fixtures/surface/dashboard.json',
  'surface-patch.schema.json': 'fixtures/patch/structure.json',
  'component-event.schema.json': 'fixtures/event/submit.json',
  'task-snapshot.schema.json': 'fixtures/task/completed-fixture.json',
  'capability-descriptor.schema.json': 'fixtures/capability/schema-exercise.json',
  'context-map.schema.json': 'fixtures/context-map/space.json',
};
for (const [file, fixture] of Object.entries(representative)) {
  try {
    const doc = JSON.parse(read(join('protocol', fixture)));
    const result = validate(doc, registry[file], registry);
    if (!result.valid) {
      problems.push(`${fixture} does not validate against ${file}: ${result.errors[0].path}: ${result.errors[0].message}`);
    }
  } catch (error) {
    problems.push(`${file} validation error: ${error.message}`);
  }
}

// ---- 3. Native wire-name parity --------------------------------------------
const semanticTypesCc = read('native/seoul/browser/semantic/semantic_types.cc');
const sauiCatalogCc = read('native/seoul/browser/saui/saui_catalog.cc');
const sauiDocumentCc = read('native/seoul/browser/saui/saui_document.cc');
const sauiPatchCc = read('native/seoul/browser/saui/saui_patch.cc');
const taskExecutionCc = read('native/seoul/browser/tasks/task_execution.cc');
const dataValidationCc = read('native/seoul/browser/data/data_validation.cc');

const semanticSchema = registry['semantic-result.schema.json'];
const surfaceSchema = registry['adaptive-surface.schema.json'];
const patchSchema = registry['surface-patch.schema.json'];
const snapshotSchema = registry['task-snapshot.schema.json'];

compareSets(
  'semantic shape',
  semanticSchema.$defs.semantic_shape.enum,
  [...semanticTypesCc.matchAll(/\{SemanticShape::k\w+, "([a-z_0-9]+)"\}/g)].map((m) => m[1]),
);
compareSets(
  'semantic role',
  semanticSchema.$defs.semantic_role.enum,
  [...semanticTypesCc.matchAll(/\{SemanticRole::k\w+, "([a-z_0-9]+)"\}/g)].map((m) => m[1]),
);
compareSets(
  'component type',
  surfaceSchema.$defs.component_type.enum,
  [...sauiCatalogCc.matchAll(/\{ComponentType::k\w+,\s*\n?\s*"([a-z_0-9]+)"/g)].map((m) => m[1]),
);
compareSets('surface kind', surfaceSchema.$defs.surface_kind.enum, switchStrings(sauiDocumentCc, 'SurfaceKindToString'));
compareSets('component state', surfaceSchema.$defs.component_state.enum, switchStrings(sauiDocumentCc, 'ComponentStateToString'));
compareSets('surface action kind', surfaceSchema.$defs.surface_action_kind.enum, switchStrings(sauiDocumentCc, 'SurfaceActionKindToString'));
compareSets('data entry kind', surfaceSchema.$defs.data_entry.properties.kind.enum, switchStrings(sauiDocumentCc, 'DataEntryKindToString'));
compareSets(
  'patch op',
  patchSchema.$defs.patch_op.oneOf.map((branch) => {
    const def = patchSchema.$defs[branch.$ref.replace('#/$defs/', '')];
    return def.properties.op.const;
  }),
  [...sauiPatchCc.matchAll(/if \(name == "([a-z_]+)"\)/g)].map((m) => m[1]),
);
compareSets('task state', snapshotSchema.properties.state.enum, switchStrings(taskExecutionCc, 'TaskStateToString'));
compareSets('step status', snapshotSchema.$defs.step_status.enum, switchStrings(taskExecutionCc, 'StepStatusToString'));
compareSets('execution route', snapshotSchema.$defs.execution_route.enum, switchStrings(taskExecutionCc, 'ExecutionRouteToString'));
compareSets('task failure reason', snapshotSchema.properties.failure.enum, switchStrings(taskExecutionCc, 'TaskFailureReasonToString'));
compareSets('freshness state', semanticSchema.$defs.freshness_state.enum, switchStrings(dataValidationCc, 'FreshnessStateToString'));

// Context Map. The graph is a projection whose vocabulary must never drift from
// the native model that produces it, so every enum is compared to the wire
// names the C++ actually emits.
const contextMapTypesCc = read('native/seoul/browser/context_map/context_map_types.cc');
const contextMapSchema = registry['context-map.schema.json'];
compareSets('context map node kind', contextMapSchema.$defs.node_kind.enum, switchStrings(contextMapTypesCc, 'NodeKindToString'));
compareSets('context map node state', contextMapSchema.$defs.node_state.enum, switchStrings(contextMapTypesCc, 'NodeStateToString'));
compareSets('context map edge type', contextMapSchema.$defs.edge_type.enum, switchStrings(contextMapTypesCc, 'EdgeTypeToString'));
compareSets('context map edge provenance', contextMapSchema.$defs.edge_provenance.enum, switchStrings(contextMapTypesCc, 'EdgeProvenanceToString'));
compareSets('context map graph scope', contextMapSchema.$defs.graph_scope.enum, switchStrings(contextMapTypesCc, 'GraphScopeToString'));

const semanticWireCc = read('native/seoul/browser/semantic/semantic_wire.cc');
const descriptorWireCc = read('native/seoul/browser/tools/tool_descriptor_wire.cc');
const snapshotWireCc = read('native/seoul/browser/product/task_snapshot_wire.cc');
const descriptorSchema = registry['capability-descriptor.schema.json'];

compareSets('field primitive', semanticSchema.$defs.field_primitive.enum, switchStrings(semanticWireCc, 'FieldPrimitiveToWire'));
compareSets('value class', semanticSchema.$defs.value_class.enum, switchStrings(semanticWireCc, 'ValueClassToWire'));
compareSets('field sensitivity', semanticSchema.$defs.field_sensitivity.enum, switchStrings(semanticWireCc, 'FieldSensitivityToWire'));
compareSets('result state', semanticSchema.properties.state.enum, switchStrings(semanticWireCc, 'ResultStateToWire'));
compareSets('data sensitivity', descriptorSchema.properties.sensitivity.enum, switchStrings(descriptorWireCc, 'DataSensitivityToWire'));
compareSets('risk category', descriptorSchema.properties.risk.enum, switchStrings(descriptorWireCc, 'RiskCategoryToWire'));
compareSets('approval policy', descriptorSchema.properties.approval.enum, switchStrings(descriptorWireCc, 'ApprovalPolicyToWire'));
compareSets('idempotency class', descriptorSchema.properties.idempotency.enum, switchStrings(descriptorWireCc, 'IdempotencyClassToWire'));
compareSets('freshness semantics', descriptorSchema.properties.freshness.enum, switchStrings(descriptorWireCc, 'FreshnessSemanticsToWire'));
compareSets('plan origin', snapshotSchema.properties.plan_origin.enum, switchStrings(snapshotWireCc, 'PlanOriginToWire'));

if (problems.length > 0) {
  console.error('check-protocol: FAILED');
  for (const problem of problems) console.error(`  - ${problem}`);
  process.exit(1);
}
console.log('check-protocol: OK (types in sync, fixtures representative-validated, native wire names match schemas)');
