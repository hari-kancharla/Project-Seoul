// Reproducible adblock-rust resource descriptors from pinned upstream metadata.
import fs from 'node:fs';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root = fileURLToPath(new URL('.', import.meta.url));
const manifest = JSON.parse(fs.readFileSync(root + 'source.json', 'utf8'));
for (const [path, expected] of Object.entries(manifest.files)) {
  const actual = crypto.createHash('sha256').update(fs.readFileSync(root + path)).digest('hex');
  if (actual !== expected) throw new Error(`Source hash mismatch: ${path}`);
}
const {builtinScriptlets} = await import('./src/js/resources/scriptlets.js');
const names = new Set(builtinScriptlets.map(item => item.name));
const resources = builtinScriptlets.map(item => {
  if (typeof item.fn !== 'function') throw new Error(`Missing implementation: ${item.name}`);
  for (const dependency of item.dependencies ?? []) {
    if (!names.has(dependency)) throw new Error(`Missing dependency: ${item.name} -> ${dependency}`);
  }
  const body = item.fn.toString();
  return {name: item.name, aliases: item.aliases ?? [], kind: 'template',
    content: Buffer.from(body).toString('base64'), dependencies: item.dependencies ?? [],
    permission: item.requiresTrust ? 1 : 0,
    world: item.name.endsWith('.fn') ? 'BOTH' : item.world ?? 'MAIN'};
}).sort((a, b) => a.name.localeCompare(b.name, 'en'));
const output = JSON.stringify(resources, null, 2) + '\n';
if (process.argv.includes('--check')) {
  if (fs.readFileSync(root + 'resources.json', 'utf8') !== output) throw new Error('Resource data is stale');
} else fs.writeFileSync(root + 'resources.json', output);
console.log(`scriptlets: ${resources.length} resources, dependencies and source hashes verified`);
