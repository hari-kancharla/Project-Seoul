#!/usr/bin/env node
// Product launch smoke test for the locally built Seoul Chromium.
//
// Uses the repo's pinned puppeteer-core (no browser download) with an explicit
// executablePath pointing at the built binary. Requires no internet (uses a
// data: URL) and does not modify Chromium source. Fails on browser disconnect
// or page crash.

import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { assertBrowserLaunchPermitted } from '../../scripts/browser-launch-safety.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');
const siblingRoot = ['seoul-chromium.noindex', 'seoul-chromium']
  .map((name) => path.resolve(repoRoot, '..', name))
  .find((p) => existsSync(p));
const root = (process.env.SEOUL_CHROMIUM_ROOT || '').trim()
  ? path.resolve(process.env.SEOUL_CHROMIUM_ROOT)
  : (siblingRoot ?? path.resolve(repoRoot, '..', 'seoul-chromium.noindex'));
const binary = (process.env.SEOUL_CHROMIUM_BINARY || '').trim()
  ? path.resolve(process.env.SEOUL_CHROMIUM_BINARY)
  : path.join(root, 'src', 'out', 'SeoulBaseline', 'Seoul.app', 'Contents', 'MacOS', 'Seoul');
const maxLaunchMs = 15000;
const maxLocalNavigationMs = 5000;
const maxCanvasReadyMs = 5000;
const maxCanvasViewSwitchMs = 1500;
const maxCanvasSwitchSoakMs = 5000;

function fail(msg) {
  console.error(`SMOKE FAIL: ${msg}`);
  process.exit(1);
}
function assert(cond, msg) {
  if (!cond) throw new Error(msg);
  console.log(`  ok: ${msg}`);
}

try {
  assertBrowserLaunchPermitted();
} catch (error) {
  fail(error instanceof Error ? error.message : String(error));
}

if (!existsSync(binary)) {
  fail(`built Seoul browser not found at:\n  ${binary}\nBuild it first (native/scripts/build.sh), or set SEOUL_CHROMIUM_BINARY.`);
}

const profile = mkdtempSync(path.join(os.tmpdir(), 'seoul-smoke-profile-'));
let browser;
let crashed = null;
let closingBrowser = false;
const smokeStarted = performance.now();

try {
  console.log(`launching: ${binary}`);
  console.log(`profile:   ${profile}`);
  const launchStarted = performance.now();
  browser = await puppeteer.launch({
    executablePath: binary,
    headless: process.env.SEOUL_HEADFUL ? false : true,
    userDataDir: profile,
    protocolTimeout: 60000,
    args: ['--no-first-run', '--no-default-browser-check', '--use-mock-keychain'],
  });
  browser.on('disconnected', () => {
    if (!closingBrowser) {
      crashed = crashed || 'browser disconnected unexpectedly';
    }
  });
  const launchMs = performance.now() - launchStarted;
  assert(
    launchMs < maxLaunchMs,
    `isolated browser launch stays below the ${maxLaunchMs} ms smoke ceiling (${launchMs.toFixed(0)} ms)`,
  );

  const version = await browser.version();
  console.log(`browser version: ${version}`);

  // A real chrome://newtab navigation must resolve to Seoul's first-party
  // Canvas document. Checking the rendered custom element catches regressions
  // where Browser::GetNewTabURL changes but the canonical NTP rewrite still
  // sends startup/omnibox new tabs to Chromium's stock page.
  const newTab = await browser.newPage();
  await newTab.goto('chrome://newtab/', {waitUntil: 'domcontentloaded'});
  await newTab.waitForFunction(async () => {
    await customElements.whenDefined('seoul-canvas-app');
    const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
    return root?.querySelector('.canvas-header h1')?.textContent?.trim() ===
        'Ask, act, understand.';
  });
  assert(
    await newTab.evaluate(() =>
      Boolean(document.querySelector('seoul-canvas-app')?.shadowRoot)),
    'chrome://newtab renders Seoul Canvas instead of the stock Chromium NTP',
  );
  await newTab.close();

  const page = await browser.newPage();
  page.on('error', (e) => {
    crashed = `page crashed: ${e.message}`;
  });

  // (3) a normal local page via data: URL (no network).
  const navigationStarted = performance.now();
  await page.goto('data:text/html,<title>Seoul Product Smoke</title><h1 id=h>hello</h1>', {
    waitUntil: 'domcontentloaded',
  });
  const navigationMs = performance.now() - navigationStarted;
  assert(
    navigationMs < maxLocalNavigationMs,
    `local navigation stays below the ${maxLocalNavigationMs} ms smoke ceiling (${navigationMs.toFixed(0)} ms)`,
  );

  // (4) JavaScript executes.
  const sum = await page.evaluate(() => 1 + 2 + 3);
  assert(sum === 6, 'JavaScript executes in the page (1+2+3===6)');
  const heading = await page.$eval('#h', (el) => el.textContent);
  assert(heading === 'hello', 'DOM is rendered and queryable');

  // (5) a second tab opens and activates.
  const page2 = await browser.newPage();
  await page2.goto('data:text/html,<title>tab2</title>second', { waitUntil: 'domcontentloaded' });
  await page2.bringToFront();
  const pages = await browser.pages();
  assert(pages.length >= 2, 'a second tab opened and is tracked');
  assert((await page2.title()) === 'tab2', 'second tab activated and reports its title');

  // (6) the shipping first-party Canvas WebUI renders its interactive shell.
  const canvas = await browser.newPage();
  const canvasErrors = [];
  canvas.on('console', (message) => {
    if (message.type() === 'error') canvasErrors.push(message.text());
  });
  canvas.on('pageerror', (error) => canvasErrors.push(String(error)));
  const canvasStarted = performance.now();
  await canvas.goto('chrome://seoul-canvas', { waitUntil: 'domcontentloaded' });
  await canvas.waitForFunction(async () => {
    await customElements.whenDefined('seoul-canvas-app');
    const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
    return Boolean(root?.querySelector('.composer input[aria-label="Message Seoul"]'));
  });
  const canvasState = await canvas.evaluate(() => {
    const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
    return {
      heading: root?.querySelector('.canvas-header h1')?.textContent?.trim(),
      views: root?.querySelectorAll('.view-switcher button').length,
      voiceOff: root?.querySelector('.voice-button')?.getAttribute('aria-pressed'),
      sendDisabled: root?.querySelector('.send-button')?.disabled,
    };
  });
  const canvasReadyMs = performance.now() - canvasStarted;
  assert(
    canvasReadyMs < maxCanvasReadyMs,
    `Seoul Canvas becomes interactive below the ${maxCanvasReadyMs} ms smoke ceiling (${canvasReadyMs.toFixed(0)} ms)`,
  );
  assert(canvasState.heading === 'Ask, act, understand.', 'Seoul Canvas renders its product heading');
  assert(canvasState.views === 5, 'Seoul Canvas exposes all five product views');
  assert(canvasState.voiceOff === 'false', 'voice remains explicit and default-off');
  assert(canvasState.sendDisabled === true, 'empty Canvas input cannot dispatch');
  assert(canvasErrors.length === 0, `Seoul Canvas reports no console errors (${canvasErrors.join('; ')})`);

  // (7) Every product surface is reachable, then repeated switching proves
  // the single Lit app does not accumulate stale selected states or crash.
  const viewSwitches = await canvas.evaluate(async (maxSwitchMs) => {
    const app = document.querySelector('seoul-canvas-app');
    const root = app?.shadowRoot;
    if (!app || !root) return { error: 'missing Canvas app' };
    const selectors = new Map([
      ['Canvas', '#canvas-root'],
      ['Boosts', '.boosts-view'],
      ['Library', '.library-view[aria-label="Library"]'],
      ['Boards', '.library-view[aria-label="Boards"]'],
      ['Studio', '.studio-view'],
    ]);
    const buttons = [...root.querySelectorAll('.view-switcher button')];
    const timings = [];
    const activate = async (name) => {
      const button = buttons.find(
          candidate => candidate.textContent.trim() === name);
      if (!button) throw new Error(`missing ${name} view button`);
      const started = performance.now();
      button.click();
      await app.updateComplete;
      const elapsed = performance.now() - started;
      const renderedView = root.querySelector(selectors.get(name));
      if (!renderedView) {
        throw new Error(`${name} view did not render`);
      }
      // Lit's updateComplete is the authoritative DOM commit boundary.
      // Forcing layout verifies the rendered view is usable without relying
      // on requestAnimationFrame, which Chromium may suspend for background
      // headless pages and thereby hang the smoke driver.
      renderedView.getBoundingClientRect();
      const selected = buttons.filter(
          candidate => candidate.getAttribute('aria-current') === 'page');
      if (selected.length !== 1 || selected[0] !== button) {
        throw new Error(`${name} selection state is inconsistent`);
      }
      if (elapsed >= maxSwitchMs) {
        throw new Error(
            `${name} switch exceeded ${maxSwitchMs} ms (${elapsed.toFixed(0)} ms)`);
      }
      timings.push({ name, elapsed });
    };
    try {
      for (const name of selectors.keys()) await activate(name);
      const soakStarted = performance.now();
      const names = [...selectors.keys()];
      for (let cycle = 0; cycle < 5; ++cycle) {
        for (const name of names) await activate(name);
      }
      return {
        timings,
        soakMs: performance.now() - soakStarted,
        composerPresent: Boolean(
          root.querySelector('.composer input[aria-label="Message Seoul"]')),
      };
    } catch (error) {
      return { error: String(error) };
    }
  }, maxCanvasViewSwitchMs);
  assert(!viewSwitches.error, `all Canvas views switch correctly (${viewSwitches.error || 'ok'})`);
  assert(
    viewSwitches.soakMs < maxCanvasSwitchSoakMs,
    `25 rapid Canvas view switches stay below the ${maxCanvasSwitchSoakMs} ms smoke ceiling (${viewSwitches.soakMs.toFixed(0)} ms)`,
  );
  assert(viewSwitches.composerPresent, 'Canvas composer survives repeated view switching');
  assert(canvasErrors.length === 0, `rapid Canvas switching reports no console errors (${canvasErrors.join('; ')})`);

  // (8) the browser stayed alive throughout.
  assert(browser.connected === true, 'browser remained connected for the test duration');
  assert(crashed === null, 'no disconnect or page crash occurred');

  // (9) version already collected above.
  assert(/Chrom(e|ium)\/\d+/.test(version), `browser reports a Chromium version string (${version})`);

  // (10) clean close.
  closingBrowser = true;
  await browser.close();
  browser = undefined;
  console.log(`total smoke time: ${(performance.now() - smokeStarted).toFixed(0)} ms`);
  console.log('SMOKE PASS');
} catch (e) {
  const failure = crashed || (e && e.message) || String(e);
  if (browser) {
    closingBrowser = true;
    await browser.close().catch(() => {});
  }
  fail(failure);
} finally {
  // (11) remove the temporary profile.
  try {
    rmSync(profile, { recursive: true, force: true });
  } catch {}
}
