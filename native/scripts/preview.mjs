#!/usr/bin/env node
// Capture the shipping Seoul Canvas WebUI from the locally built Seoul Chromium.
//
// This intentionally refuses to discover or launch an installed browser. The
// executable must be the local Seoul build (or an explicit test override).

import { existsSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { assertBrowserLaunchPermitted } from '../../scripts/browser-launch-safety.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');
const siblingRoot = ['seoul-chromium.noindex', 'seoul-chromium']
  .map((name) => path.resolve(repoRoot, '..', name))
  .find((candidate) => existsSync(candidate));
const chromiumRoot = (process.env.SEOUL_CHROMIUM_ROOT || '').trim()
  ? path.resolve(process.env.SEOUL_CHROMIUM_ROOT)
  : (siblingRoot ?? path.resolve(repoRoot, '..', 'seoul-chromium.noindex'));
const binary = (process.env.SEOUL_CHROMIUM_BINARY || '').trim()
  ? path.resolve(process.env.SEOUL_CHROMIUM_BINARY)
  : path.join(
      chromiumRoot,
      'src',
      'out',
      'SeoulBaseline',
      'Chromium.app',
      'Contents',
      'MacOS',
      'Chromium',
    );
const output = (process.env.SEOUL_PREVIEW_PATH || '').trim()
  ? path.resolve(process.env.SEOUL_PREVIEW_PATH)
  : path.join(os.tmpdir(), 'seoul-canvas-preview.png');

function fail(message) {
  console.error(`PREVIEW FAIL: ${message}`);
  process.exitCode = 1;
}

let launchBlocked = '';
try {
  assertBrowserLaunchPermitted();
} catch (error) {
  launchBlocked = error instanceof Error ? error.message : String(error);
}

if (launchBlocked) {
  fail(launchBlocked);
} else if (!existsSync(binary)) {
  fail(`local Seoul Chromium build not found at:\n  ${binary}`);
} else {
  mkdirSync(path.dirname(output), { recursive: true });
  const profile = mkdtempSync(path.join(os.tmpdir(), 'seoul-preview-profile-'));
  let browser;
  let closingBrowser = false;
  let disconnected = false;

  try {
    browser = await puppeteer.launch({
      executablePath: binary,
      headless: true,
      userDataDir: profile,
      protocolTimeout: 60000,
      defaultViewport: {
        width: 1440,
        height: 1000,
        deviceScaleFactor: 1,
      },
      args: [
        '--no-first-run',
        '--no-default-browser-check',
        '--use-mock-keychain',
        '--force-prefers-reduced-motion',
      ],
    });
    browser.on('disconnected', () => {
      if (!closingBrowser) disconnected = true;
    });

    const page = await browser.newPage();
    if (process.env.SEOUL_PREVIEW_COLOR_SCHEME === 'dark') {
      await page.emulateMediaFeatures([
        { name: 'prefers-color-scheme', value: 'dark' },
      ]);
    }
    const errors = [];
    page.on('console', (message) => {
      if (message.type() === 'error') errors.push(message.text());
    });
    page.on('pageerror', (error) => errors.push(String(error)));

    await page.goto('chrome://seoul-canvas', { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(async () => {
      await customElements.whenDefined('seoul-canvas-app');
      const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
      return Boolean(
        root?.querySelector('.canvas-header h1') &&
          root?.querySelector('.composer input[aria-label="Message Seoul"]'),
      );
    });
    const requestedView = (process.env.SEOUL_PREVIEW_VIEW || '').trim().toLowerCase();
    if (requestedView) {
      const viewSelectors = {
        canvas: '#canvas-root',
        boosts: '.boosts-view',
        library: '.library-view[aria-label="Library"]',
        boards: '.library-view[aria-label="Boards"]',
        studio: '.studio-view',
      };
      const readySelector = viewSelectors[requestedView];
      if (!readySelector) {
        throw new Error(
          `Unknown SEOUL_PREVIEW_VIEW "${requestedView}". ` +
            `Expected one of: ${Object.keys(viewSelectors).join(', ')}`,
        );
      }
      const selected = await page.evaluate(async (view) => {
        const app = document.querySelector('seoul-canvas-app');
        const root = app?.shadowRoot;
        if (!app || !root) return false;
        const button = [...root.querySelectorAll('.view-switcher button')]
            .find(candidate => candidate.textContent.trim().toLowerCase() === view);
        if (!button) return false;
        button.click();
        await app.updateComplete;
        return true;
      }, requestedView);
      if (!selected) {
        throw new Error(`Canvas view "${requestedView}" is not reachable`);
      }
      await page.waitForSelector(`seoul-canvas-app`);
      await page.waitForFunction((selector) => {
        const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
        return Boolean(root?.querySelector(selector));
      }, {}, readySelector);
    }
    if (process.env.SEOUL_PREVIEW_SCENARIO === 'board') {
      const result = await page.evaluate(async () => {
        const app = document.querySelector('seoul-canvas-app');
        const root = app?.shadowRoot;
        if (!app || !root) return 'missing-app';
        const waitFor = async (check) => {
          for (let attempt = 0; attempt < 160; ++attempt) {
            const value = check();
            if (value) return value;
            await new Promise(resolve => setTimeout(resolve, 25));
          }
          return null;
        };
        const dispatchInput = (control, value) => {
          control.value = value;
          control.dispatchEvent(new Event('input', {
            bubbles: true,
            composed: true,
          }));
        };
        const boardsTab = [...root.querySelectorAll('.view-switcher button')]
            .find(button => button.textContent.trim() === 'Boards');
        boardsTab?.click();
        const boardInput = await waitFor(
            () => root.querySelector('.board-create input'));
        if (!boardInput) return 'missing-board-form';
        dispatchInput(boardInput, 'Product architecture');
        await app.updateComplete;
        root.querySelector('.board-create')?.requestSubmit();
        const openBoard = await waitFor(
            () => root.querySelector('.board-actions .primary'));
        if (!openBoard) return 'board-not-created';
        openBoard.click();
        const addNote = await waitFor(
            () => root.querySelector('.board-toolbar button'));
        if (!addNote) return 'editor-not-opened';
        addNote.click();
        const note = await waitFor(
            () => root.querySelector('.board-element-form textarea'));
        if (!note) return 'note-form-missing';
        const title = root.querySelector('.board-element-form input');
        if (title) dispatchInput(title, 'The Seoul control loop');
        dispatchInput(
            note,
            'Observe browser context. Plan typed work. Ask before impact. Verify the outcome.');
        await app.updateComplete;
        root.querySelector('.board-element-form')?.requestSubmit();
        return await waitFor(() => root.querySelector('.board-element')) ?
            'ready' : 'element-not-created';
      });
      if (result !== 'ready') {
        throw new Error(`Board preview setup failed: ${result}`);
      }
    }
    if (process.env.SEOUL_PREVIEW_DEBUG) {
      const diagnostics = await page.evaluate(() => {
        const root = document.querySelector('seoul-canvas-app')?.shadowRoot;
        const bounds = (selector) => {
          const rect = root?.querySelector(selector)?.getBoundingClientRect();
          return rect ? {
            top: rect.top,
            right: rect.right,
            bottom: rect.bottom,
            left: rect.left,
            width: rect.width,
            height: rect.height,
          } : null;
        };
        const canvasRoot = root?.querySelector('#canvas-root');
        return {
          documentScrollY: window.scrollY,
          canvasScrollTop: canvasRoot?.scrollTop ?? null,
          header: bounds('.canvas-header'),
          heading: bounds('.canvas-header h1'),
          switcher: bounds('.view-switcher'),
          idle: bounds('.idle'),
        };
      });
      console.log(`Layout: ${JSON.stringify(diagnostics)}`);
    }
    await page.screenshot({
      path: output,
      type: 'png',
      captureBeyondViewport: false,
    });

    if (errors.length > 0) {
      throw new Error(`Canvas console errors: ${errors.join('; ')}`);
    }
    if (disconnected) {
      throw new Error('Seoul Chromium disconnected while capturing the preview');
    }

    console.log(`Seoul binary:  ${binary}`);
    console.log(`Preview image: ${output}`);
    console.log('PREVIEW PASS');
  } catch (error) {
    fail(error instanceof Error ? error.message : String(error));
  } finally {
    if (browser) {
      closingBrowser = true;
      await browser.close().catch(() => {});
    }
    rmSync(profile, { recursive: true, force: true });
  }
}
