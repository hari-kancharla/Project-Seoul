import assert from 'node:assert/strict';
import {Buffer} from 'node:buffer';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

import esbuild from 'esbuild';

const here = path.dirname(fileURLToPath(import.meta.url));
const bundle = await esbuild.build({
  entryPoints: [path.join(here, 'canvas_types.ts')],
  bundle: true,
  format: 'esm',
  platform: 'node',
  write: false,
});
const types = await import(
    `data:text/javascript;base64,${
      Buffer.from(bundle.outputFiles[0].text).toString('base64')}`);

test('Boost editor binding preserves pause and Scene state', () => {
  const active = {
    tab_id: 'tab-original',
    title: 'Current page',
    origin: 'https://current.example',
    customizable: true,
  };
  const layer = {
    schema_version: 1,
    id: 'layer',
    name: 'Paused Scene Boost',
    origin_pattern: '*.target.example',
    scene_scope: 'focus-scene',
    enabled: false,
    adjustments: [],
  };
  assert.deepEqual(types.siteLayerEditorBinding(active, layer), {
    tabId: 'tab-original',
    pageOrigin: 'https://current.example',
    originPattern: '*.target.example',
    sceneScope: 'focus-scene',
    enabled: false,
  });
});

test('Boost editor binding rejects unsupported pages and changed tabs', () => {
  const unsupported = {
    tab_id: 'internal-tab',
    title: 'Settings',
    origin: '',
    customizable: false,
  };
  assert.equal(types.siteLayerEditorBinding(unsupported), undefined);

  const binding = types.siteLayerEditorBinding({
    tab_id: 'tab-a',
    title: 'A',
    origin: 'https://a.example',
    customizable: true,
  });
  assert.equal(types.siteLayerEditorBindingMatches(binding, {
    tab_id: 'tab-b',
    title: 'B',
    origin: 'https://a.example',
    customizable: true,
  }), false);
  assert.equal(types.siteLayerEditorBindingMatches(binding, {
    tab_id: 'tab-a',
    title: 'A moved',
    origin: 'https://b.example',
    customizable: true,
  }), false);
});

test('Boost editor preserves adjustments it does not render', () => {
  const adjustments = [
    {kind: 'density', density: 'compact'},
    {kind: 'emphasize', selectors: ['main']},
    {kind: 'sticky_header_off', selectors: ['header']},
    {
      kind: 'future_adjustment',
      selectors: ['article'],
      color_value: '#123456',
      numeric_value: .4,
    },
    {kind: 'text_color', selectors: ['body'], color_value: '#111111'},
    {kind: 'hide', selectors: ['.ad']},
  ];
  assert.deepEqual(
      types.boostPassthroughAdjustments(adjustments),
      [
        {
          kind: 'density',
          selectors: [],
          textValue: '',
          numericValue: 0,
          density: 'compact',
        },
        {
          kind: 'emphasize',
          selectors: ['main'],
          textValue: '',
          numericValue: 0,
          density: 'comfortable',
        },
        {
          kind: 'sticky_header_off',
          selectors: ['header'],
          textValue: '',
          numericValue: 0,
          density: 'comfortable',
        },
        {
          kind: 'future_adjustment',
          selectors: ['article'],
          textValue: '#123456',
          numericValue: .4,
          density: 'comfortable',
        },
      ]);
});

test('more than eight Zaps are emitted as valid selector chunks', () => {
  const selectors =
      Array.from({length: 19}, (_, index) => `#zap-${index + 1}`);
  const chunks = types.chunkBoostHideSelectors(selectors);
  assert.deepEqual(chunks.map(chunk => chunk.selectors.length), [8, 8, 3]);
  assert.deepEqual(
      chunks.flatMap(chunk => chunk.selectors), selectors);
  assert.ok(chunks.every(chunk => chunk.kind === 'hide'));
  assert.throws(
      () => types.chunkBoostHideSelectors(selectors, 0), RangeError);
});
