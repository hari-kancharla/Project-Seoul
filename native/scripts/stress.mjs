#!/usr/bin/env node
// Churn exercise for the locally built Seoul Chromium.
//
// smoke.mjs answers "does the product come up and work once". This answers the
// question after it: does it still work after being used hard. It repeatedly
// mounts and unmounts the first-party Canvas, runs a dozen of them at once,
// churns activation across them, drives navigation and history, then closes
// everything and checks the product is still healthy.
//
// The failures this is looking for do not show up in a single pass: listeners
// and observers that are added on mount and never removed, a Canvas that only
// survives its first mount, renderer crashes that need concurrency to trigger,
// and JS heap that grows with every remount. Console errors are fatal here for
// the same reason they are in the smoke - the shipping Canvas is expected to
// run clean, so any error at all is a real finding.
//
// This is not a long-session soak and does not claim to be one; the soak
// remains a release gate. It uses the same pinned puppeteer-core and the same
// explicit binary as the smoke, downloads nothing, needs no network, and never
// falls back to an installed browser.

import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import puppeteer from 'puppeteer-core';
import { assertBrowserLaunchPermitted } from '../../scripts/browser-launch-safety.mjs';
import { productBinary } from './checkout-root.mjs';

const binary = productBinary();

const REMOUNTS = Number(process.env.SEOUL_STRESS_REMOUNTS || 15);
const TABS = Number(process.env.SEOUL_STRESS_TABS || 12);
const ACTIVATION_ROUNDS = Number(process.env.SEOUL_STRESS_ROUNDS || 5);
const NAVIGATION_CYCLES = Number(process.env.SEOUL_STRESS_NAVIGATIONS || 8);
// Remounting the same document should not retain anything. A ceiling rather
// than an exact figure: this is a leak detector, not a memory budget.
const MAX_HEAP_GROWTH_RATIO = 2.5;

function fail(msg) {
  console.error(`STRESS FAIL: ${msg}`);
  process.exit(1);
}

try {
  assertBrowserLaunchPermitted();
} catch (error) {
  fail(error instanceof Error ? error.message : String(error));
}

if (!existsSync(binary)) {
  fail(
    `built Seoul browser not found at:\n  ${binary}\n` +
      'Build it first (native/scripts/build.sh), or set SEOUL_CHROMIUM_BINARY.',
  );
}

const problems = [];
const note = (what) => {
  problems.push(what);
  console.error(`  PROBLEM: ${what}`);
};

const profileDir = mkdtempSync(path.join(os.tmpdir(), 'seoul-stress-'));
let browser;
let closingOnPurpose = false;

// Any page the exercise touches reports its own failures, labelled, so a
// problem found in tab 7 of 12 says so instead of arriving anonymously.
function watch(page, label) {
  page.on('console', (m) => {
    if (m.type() === 'error') {
      note(`console error on ${label}: ${m.text().slice(0, 200)}`);
    }
  });
  page.on('pageerror', (e) => note(`page error on ${label}: ${String(e).slice(0, 200)}`));
  page.on('error', (e) => note(`RENDERER CRASH on ${label}: ${String(e).slice(0, 200)}`));
}

const heapOf = (page) =>
  page.evaluate(() => (performance.memory ? performance.memory.usedJSHeapSize : 0));

try {
  browser = await puppeteer.launch({
    headless: true,
    executablePath: binary,
    userDataDir: profileDir,
    args: [
      '--no-first-run',
      '--no-default-browser-check',
      '--use-mock-keychain',
      '--disable-features=InitialWebUI',
    ],
  });
  browser.on('disconnected', () => {
    if (!closingOnPurpose) note('browser disconnected mid-run');
  });

  const first = (await browser.pages())[0];
  watch(first, 'initial');

  console.log(`1. Canvas mount/unmount x${REMOUNTS}`);
  await first.goto('chrome://seoul-canvas/', { waitUntil: 'domcontentloaded' });
  const heapFirstMount = await heapOf(first);
  for (let i = 0; i < REMOUNTS; i++) {
    await first.goto('about:blank', { waitUntil: 'domcontentloaded' });
    await first.goto('chrome://seoul-canvas/', { waitUntil: 'domcontentloaded' });
  }
  const heapAfterRemounts = await heapOf(first);
  if (heapFirstMount > 0 && heapAfterRemounts > heapFirstMount * MAX_HEAP_GROWTH_RATIO) {
    note(
      `JS heap grew ${(heapAfterRemounts / heapFirstMount).toFixed(1)}x over ` +
        `${REMOUNTS} Canvas remounts (${(heapFirstMount / 1e6).toFixed(1)} -> ` +
        `${(heapAfterRemounts / 1e6).toFixed(1)} MB); something is retained across mounts`,
    );
  }

  console.log(`2. ${TABS} concurrent Canvas tabs`);
  const tabs = [];
  for (let i = 0; i < TABS; i++) {
    const page = await browser.newPage();
    watch(page, `tab${i}`);
    await page.goto('chrome://seoul-canvas/', { waitUntil: 'domcontentloaded' });
    tabs.push(page);
  }

  console.log(`3. ${TABS * ACTIVATION_ROUNDS} rapid tab activations`);
  for (let round = 0; round < ACTIVATION_ROUNDS; round++) {
    for (const page of tabs) {
      await page.bringToFront();
    }
  }

  console.log(`4. ${NAVIGATION_CYCLES} navigation cycles against a heavy document, plus history`);
  const heavy =
    'data:text/html,' +
    encodeURIComponent(
      `<html><body>${'<div class="row">row</div>'.repeat(4000)}</body></html>`,
    );
  for (let i = 0; i < NAVIGATION_CYCLES; i++) {
    await tabs[0].goto(heavy, { waitUntil: 'load' });
    await tabs[0].goto('chrome://seoul-canvas/', { waitUntil: 'domcontentloaded' });
  }
  await tabs[0].goBack({ waitUntil: 'domcontentloaded' }).catch(() => {});
  await tabs[0].goForward({ waitUntil: 'domcontentloaded' }).catch(() => {});

  console.log('5. closing every tab');
  for (const page of tabs) {
    await page.close();
  }
  if (!browser.connected) {
    note('browser did not survive closing every tab');
  }

  console.log('6. Canvas still mounts and renders after all of it');
  const last = await browser.newPage();
  watch(last, 'final');
  await last.goto('chrome://seoul-canvas/', { waitUntil: 'domcontentloaded' });
  const rendered = await last.evaluate(() => {
    const body = document.body;
    return !!body && body.children.length > 0;
  });
  if (!rendered) {
    note('Canvas rendered an empty document after the churn');
  }

  console.log(
    `\nJS heap: ${(heapFirstMount / 1e6).toFixed(1)} MB on first mount, ` +
      `${(heapAfterRemounts / 1e6).toFixed(1)} MB after ${REMOUNTS} remounts`,
  );
} catch (error) {
  note(`exercise threw: ${error instanceof Error ? error.message : String(error)}`);
} finally {
  closingOnPurpose = true;
  if (browser) {
    await browser.close().catch(() => {});
  }
  rmSync(profileDir, { recursive: true, force: true });
}

if (problems.length > 0) {
  console.error(`\nSTRESS FAIL: ${problems.length} problem(s) found`);
  process.exit(1);
}
console.log('\nSTRESS PASS: no defects observed under churn');
