#!/usr/bin/env node
// Bundles the content script.
//
// MV3 content scripts declared in the manifest are NOT ES modules — Chrome
// loads them as classic scripts, so a bare `import` throws at injection time
// and the script silently never runs. Bundling to an IIFE is the fix.
//
// The background service worker IS a module ("type": "module" in the manifest)
// and imports nothing, so it ships as-is.

import { build } from 'esbuild';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));

await build({
  entryPoints: [path.join(here, 'src/content/contentScript.mjs')],
  outfile: path.join(here, 'dist/content.js'),
  bundle: true,
  format: 'iife',
  target: 'chrome116',
  platform: 'browser',
  legalComments: 'none',
  logLevel: 'info',
});

console.log('bundled -> apps/browser-harness/dist/content.js');
