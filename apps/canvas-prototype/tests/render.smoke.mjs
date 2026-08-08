// Headless render smoke test: loads the built self-contained HTML in a real
// browser and drives the actual fixture loop - run a goal, confirm an
// adaptive artifact renders (a real SVG chart), switch its representation and
// confirm the SAME artifact element is PATCHED (only the root component's
// element is re-rendered; the artifact, its margin, and focus/scroll/selection
// all survive), receive a synthetic stream batch through the patch engine,
// and confirm the catalog is generated from the registered descriptors with
// honest wording ("fixture contract validated", "synthetic demo data").
// Fails on any console error.

import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { launchBrowser, findBrowserBinary, NO_BROWSER_MESSAGE } from '../launch-browser.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const htmlUrl = pathToFileURL(path.join(here, '..', 'dist', 'index.html')).href;

// This case needs a real browser, and the resolver deliberately refuses to
// borrow the user's installed Chrome on macOS. On a machine that has not built
// Seoul there is therefore nothing legitimate to drive, and the honest outcome
// is a stated skip rather than a red suite - the same convention the native
// syntax gate uses when the pinned checkout is absent.
//
// A skip that nobody notices is how a suite quietly stops testing anything, so
// CI sets SEOUL_REQUIRE_BROWSER_TESTS=1: where a browser is guaranteed, its
// disappearance is a failure, not a shrug.
const browserBinary = findBrowserBinary();
const required = process.env.SEOUL_REQUIRE_BROWSER_TESTS === '1';
if (!browserBinary && required) {
  throw new Error(
    `SEOUL_REQUIRE_BROWSER_TESTS=1 but ${NO_BROWSER_MESSAGE.charAt(0).toLowerCase()}${NO_BROWSER_MESSAGE.slice(1)}`,
  );
}
const skip = browserBinary
  ? false
  : `${NO_BROWSER_MESSAGE} Set SEOUL_REQUIRE_BROWSER_TESTS=1 to make this a failure.`;

test('design lab renders, patches in place, and preserves focus and scroll', { skip }, async () => {
  const browser = await launchBrowser({ args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
    page.on('pageerror', (e) => errors.push(String(e)));
    await page.goto(htmlUrl, { waitUntil: 'networkidle0' });

    // The Canvas opens with a first artifact (the latency timeline -> chart).
    await page.waitForSelector('.artifact', { timeout: 5000 });
    assert.ok(await page.$('.artifact .chart-svg'), 'first artifact should render a real SVG chart');
    const pathCount = await page.$$eval('.artifact .chart-svg path', (n) => n.length);
    assert.ok(pathCount >= 1, 'chart should draw at least one series path');

    // Honest wording: the margin says the fixture CONTRACT was validated and
    // labels the data synthetic; it never claims observation-verified.
    const marginText = await page.$eval('.artifact-margin', (el) => el.textContent);
    assert.match(marginText, /fixture contract validated/);
    assert.match(marginText, /synthetic demo data/);
    assert.doesNotMatch(marginText, /(^|[^"])verified/);

    // Every artifact carries a visible spoken-insight line, and the voice
    // toggle is explicit and default-off.
    const insight = await page.$eval('.artifact .artifact-insight', (el) => el.textContent);
    assert.ok(insight && insight.length > 10, 'artifact renders its insight line');
    const voiceDefaultOff = await page.$$eval('.ghost-btn', (btns) => {
      const b = btns.find((x) => /Voice/.test(x.textContent));
      return b ? { pressed: b.getAttribute('aria-pressed'), label: b.textContent } : null;
    });
    assert.ok(voiceDefaultOff, 'voice toggle exists');
    assert.equal(voiceDefaultOff.pressed, 'false', 'voice is opt-in, never on by default');
    await page.$$eval('.ghost-btn', (btns) => btns.find((x) => /Voice/.test(x.textContent)).click());
    const voiceOn = await page.$$eval('.ghost-btn', (btns) => btns.find((x) => /Voice/.test(x.textContent)).getAttribute('aria-pressed'));
    assert.equal(voiceOn, 'true', 'toggle turns voice on');

    // The catalog index is generated from the registered descriptors.
    const indexRows = await page.$$eval('.index-row', (rows) => rows.length);
    assert.equal(indexRows, 20, 'catalog rows come from the 20 registered fixture descriptors');
    assert.match(await page.$eval('.index-note', (el) => el.textContent), /[Ss]ynthetic/);

    // Representation switch = a real incremental patch: mark the artifact,
    // its margin, and the body wrapper; focus the composer with a selection;
    // record the stack scroll. All must survive the patch, and only the root
    // component's element may be replaced.
    const scrollBefore = await page.evaluate(() => {
      const artifact = document.querySelector('.artifact');
      artifact.__seoulMarker = 'artifact';
      artifact.querySelector('.artifact-margin').__seoulMarker = 'margin';
      artifact.querySelector('.artifact-body').__seoulMarker = 'body';
      const input = document.querySelector('.composer-input');
      input.value = 'inspect selection survives';
      input.focus();
      input.setSelectionRange(8, 17);
      const stack = document.querySelector('.stack');
      stack.scrollTo({ top: 40, behavior: 'instant' });
      return stack.scrollTop;
    });
    assert.ok(scrollBefore > 0, 'the artifact stack must actually scroll');

    const artifactsBefore = await page.$$eval('.artifact', (n) => n.length);
    const clicked = await page.$$eval('.rep-btn', (btns) => {
      const t = btns.find((b) => b.textContent.trim() === 'Table');
      if (t) t.click();
      return !!t;
    });
    assert.ok(clicked, 'a Table representation switch should be offered');
    await page.waitForSelector('.artifact .data-table');

    const after = await page.evaluate(() => {
      const artifact = document.querySelector('.artifact');
      const input = document.querySelector('.composer-input');
      return {
        artifactCount: document.querySelectorAll('.artifact').length,
        artifactMarker: artifact.__seoulMarker ?? null,
        marginMarker: artifact.querySelector('.artifact-margin').__seoulMarker ?? null,
        bodyMarker: artifact.querySelector('.artifact-body').__seoulMarker ?? null,
        activeIsComposer: document.activeElement === input,
        selection: [input.selectionStart, input.selectionEnd],
        stackScroll: document.querySelector('.stack').scrollTop,
        hasChart: !!artifact.querySelector('.chart-svg'),
      };
    });
    assert.equal(after.artifactCount, artifactsBefore, 'switch must not duplicate the artifact');
    assert.equal(after.artifactMarker, 'artifact', 'the artifact element must keep its DOM identity');
    assert.equal(after.marginMarker, 'margin', 'the margin receipt must keep its DOM identity');
    assert.equal(after.bodyMarker, 'body', 'the body wrapper is patched within, not replaced');
    assert.equal(after.activeIsComposer, true, 'focus must survive the patch');
    assert.deepEqual(after.selection, [8, 17], 'text selection must survive the patch');
    assert.equal(
      after.stackScroll,
      scrollBefore,
      'the browser-normalized scroll position must survive the patch',
    );
    assert.equal(after.hasChart, false, 'the chart was replaced by the table');

    // A different capability appends a second, differently-shaped artifact.
    await page.evaluate(() => { document.querySelector('.composer-input').value = ''; });
    await page.type('.composer-input', 'describe the compute node profile with region and status');
    await page.keyboard.press('Enter');
    await page.waitForFunction(
      (n) => document.querySelectorAll('.artifact').length > n,
      {},
      artifactsBefore,
    );
    assert.ok(await page.$('.artifact .card-grid, .artifact .kv'), 'record renders as a card');

    // Hierarchy renders a tree.
    await page.type('.composer-input', 'list the workspace project hierarchy tree');
    await page.keyboard.press('Enter');
    await page.waitForSelector('.artifact .tree');

    // Streaming: the artifact updates in place through the patch engine.
    await page.type('.composer-input', 'stream the live queue depth');
    await page.keyboard.press('Enter');
    await page.waitForSelector('.stream-controls');
    const streamState = await page.evaluate(() => {
      const root = document.getElementById('root');
      const before = root.seoulDesignLab.surfaces()[0];
      const artifact = [...document.querySelectorAll('.artifact')].at(-1);
      artifact.__seoulMarker = 'stream-artifact';
      document.querySelector('.stream-controls .rep-btn').click();
      const afterRev = root.seoulDesignLab.surfaces()[0];
      return {
        surfaceUnchanged: before.surfaceId === afterRev.surfaceId,
        revisionBefore: before.revision,
        revisionAfter: afterRev.revision,
        markerSurvived: artifact.__seoulMarker === 'stream-artifact',
        stillHasChart: !!artifact.querySelector('.chart-svg'),
      };
    });
    assert.equal(streamState.surfaceUnchanged, true, 'the stream batch updates the SAME surface');
    assert.equal(streamState.revisionAfter, streamState.revisionBefore + 1, 'one atomic patch, one revision bump');
    assert.equal(streamState.markerSurvived, true, 'the streaming artifact keeps its DOM identity');
    assert.equal(streamState.stillHasChart, true, 'the chart survives the in-place data patch');

    assert.deepEqual(errors, [], `no console errors during the loop:\n${errors.join('\n')}`);
  } finally {
    await browser.close();
  }
});
