#!/usr/bin/env node
// Generates protocol/ts/types.ts from the canonical schemas. The schemas are
// the source of truth; the emitted file is checked in and drift-gated by
// scripts/check-protocol.mjs. Deterministic: output depends only on the
// schema files, in a fixed order.
import { readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const protocolDir = join(repoRoot, 'protocol');

const SCHEMA_FILES = [
  ['semantic-result.schema.json', 'SemanticResult'],
  ['adaptive-surface.schema.json', 'AdaptiveSurface'],
  ['surface-patch.schema.json', 'SurfacePatch'],
  ['component-event.schema.json', 'ComponentEvent'],
  ['task-snapshot.schema.json', 'TaskSnapshot'],
  ['capability-descriptor.schema.json', 'CapabilityDescriptor'],
  ['context-map.schema.json', 'ContextMapGraph'],
];

const registry = new Map();
for (const [file] of SCHEMA_FILES) {
  registry.set(file, JSON.parse(readFileSync(join(protocolDir, file), 'utf8')));
}

function pascal(name) {
  return name
    .split(/[_-]/)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join('');
}

const defNames = new Map(); // "file#def" -> emitted type name
for (const [file] of SCHEMA_FILES) {
  const defs = registry.get(file).$defs ?? {};
  for (const key of Object.keys(defs)) {
    const name = pascal(key);
    for (const [otherKey, otherName] of defNames) {
      if (otherName === name && otherKey !== `${file}#${key}`) {
        throw new Error(`type name collision: ${name} from ${otherKey} and ${file}#${key}`);
      }
    }
    defNames.set(`${file}#${key}`, name);
  }
}

function refName(ref, currentFile) {
  const hash = ref.indexOf('#');
  const file = hash > 0 ? ref.slice(0, hash) : currentFile;
  const pointer = ref.slice(hash);
  const match = pointer.match(/^#\/\$defs\/([A-Za-z0-9_-]+)$/);
  if (!match) throw new Error(`unsupported $ref shape: ${ref}`);
  const name = defNames.get(`${file}#${match[1]}`);
  if (!name) throw new Error(`$ref to unknown def: ${ref} (from ${currentFile})`);
  return name;
}

function literal(value) {
  return JSON.stringify(value);
}

function typeFor(schema, currentFile, indent) {
  if (schema.$ref !== undefined) return refName(schema.$ref, currentFile);
  if (schema.const !== undefined) return literal(schema.const);
  if (schema.enum !== undefined) return schema.enum.map(literal).join(' | ');
  if (schema.oneOf !== undefined) {
    return schema.oneOf.map((branch) => typeFor(branch, currentFile, indent)).join(' | ');
  }
  switch (schema.type) {
    case 'string':
      return 'string';
    case 'number':
    case 'integer':
      return 'number';
    case 'boolean':
      return 'boolean';
    case 'array':
      return schema.items ? `Array<${typeFor(schema.items, currentFile, indent)}>` : 'unknown[]';
    case 'object':
      return objectTypeFor(schema, currentFile, indent);
    default:
      return 'unknown';
  }
}

function objectTypeFor(schema, currentFile, indent) {
  const props = schema.properties ?? {};
  const keys = Object.keys(props);
  if (keys.length === 0) {
    if (schema.additionalProperties && schema.additionalProperties !== true) {
      return `Record<string, ${typeFor(schema.additionalProperties, currentFile, indent)}>`;
    }
    return 'Record<string, unknown>';
  }
  const required = new Set(schema.required ?? []);
  const pad = '  '.repeat(indent + 1);
  const lines = keys.map((key) => {
    const opt = required.has(key) ? '' : '?';
    const doc = props[key].description
      ? `${pad}/** ${props[key].description.replaceAll('*/', '*\\/')} */\n`
      : '';
    return `${doc}${pad}${JSON.stringify(key)}${opt}: ${typeFor(props[key], currentFile, indent + 1)};`;
  });
  return `{\n${lines.join('\n')}\n${'  '.repeat(indent)}}`;
}

const out = [];
out.push('// GENERATED FILE - do not edit.');
out.push('// Derived from the canonical schemas in protocol/ by');
out.push('// scripts/generate-protocol-types.mjs; drift is gated by');
out.push('// scripts/check-protocol.mjs (npm run ci).');
out.push('');
out.push('export const PROTOCOL_VERSION = 1;');
out.push('');

for (const [file, rootName] of SCHEMA_FILES) {
  const schema = registry.get(file);
  out.push(`// ---- ${file} ----`);
  out.push('');
  const defs = schema.$defs ?? {};
  for (const [key, def] of Object.entries(defs)) {
    const name = defNames.get(`${file}#${key}`);
    if (def.description) out.push(`/** ${def.description.replaceAll('*/', '*\\/')} */`);
    if (def.enum !== undefined || def.oneOf !== undefined || def.type !== 'object' || (def.properties === undefined && def.additionalProperties !== undefined)) {
      out.push(`export type ${name} = ${typeFor(def, file, 0)};`);
    } else {
      out.push(`export interface ${name} ${objectTypeFor(def, file, 0)}`);
    }
    out.push('');
  }
  if (schema.description) out.push(`/** ${schema.description.replaceAll('*/', '*\\/')} */`);
  out.push(`export interface ${rootName} ${objectTypeFor(schema, file, 0)}`);
  out.push('');
}

const target = join(protocolDir, 'ts', 'types.ts');
const content = out.join('\n');
if (process.argv.includes('--check')) {
  const current = readFileSync(target, 'utf8');
  if (current !== content) {
    console.error('protocol/ts/types.ts is stale; run: node scripts/generate-protocol-types.mjs');
    process.exit(1);
  }
  console.log('protocol types in sync with schemas');
} else {
  writeFileSync(target, content);
  console.log(`generated ${target}`);
}
