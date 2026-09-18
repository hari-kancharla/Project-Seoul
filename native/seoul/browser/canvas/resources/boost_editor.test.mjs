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

test('Boost summaries count code and explain execution gates', () => {
  const layer = {enabled: true, scene_scope: '', adjustments: [],
    has_custom_css: false, has_custom_javascript: true};
  const settings = {boosts_enabled: true, javascript_enabled: false};
  assert.deepEqual(types.boostSummary(layer, settings),
      {changes: 1, state: 'JavaScript off', javascriptOff: true});
  assert.equal(types.boostSummary({...layer, has_custom_css: true}, settings).state,
      'Enabled');
  assert.equal(types.boostSummary({...layer, has_custom_css: true}, settings).changes,
      2);
  assert.equal(types.boostSummary(layer, {...settings, boosts_enabled: false}).state,
      'Off in Settings');
  assert.equal(types.boostSummary({...layer, enabled: false}, settings).state,
      'Paused');
  assert.equal(types.boostSummary({...layer, has_custom_javascript: false}, settings).state,
      'Empty');
  assert.equal(types.boostSummary({...layer, scene_scope: 'focus',
    matches_active_scene: false}, {...settings, javascript_enabled: true}).state,
      'Scene inactive');
});

test('delayed Boost replies cannot replace a newer pushed snapshot', () => {
  assert.equal(types.isOlderBoostSnapshot({revision: '9007199254740992'},
      {revision: '9007199254740993'}), true);
  assert.equal(types.isOlderBoostSnapshot({revision: '20'}, {revision: '20'}), false);
  assert.equal(types.isOlderBoostSnapshot({revision: '21'}, {revision: '20'}), false);
  assert.equal(types.isOlderBoostSnapshot({revision: '1'}, {}), false);
});

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
