// Counts ad/tracker requests a real page attempts, and how many the blocker
// stopped. Headless and CDP-driven: nothing is injected into the user's input.
import puppeteer from 'puppeteer-core';
import { mkdtempSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { productBinary } from './native/scripts/checkout-root.mjs';

// Resolved, never spelled out: a hardcoded path is right on exactly one machine
// and silently wrong everywhere else, which check-checkout-resolution.mjs exists
// to prevent.
const BIN = productBinary();
const AD_HOSTS = /doubleclick|googlesyndication|googletagservices|googletagmanager|google-analytics|adservice|adnxs|criteo|taboola|outbrain|scorecardresearch|amazon-adsystem|pubmatic|rubiconproject|openx|casalemedia|33across|sharethrough|adsafeprotected|moatads|quantserve|bluekai|demdex|krxd|segment\.io|hotjar|fullstory|mouseflow/i;
const sites = process.argv.slice(2);

const browser = await puppeteer.launch({
  executablePath: BIN, headless: false,
  userDataDir: mkdtempSync(path.join(os.tmpdir(), 'seoul-adcheck-')),
  args: ['--no-first-run', '--no-default-browser-check', '--window-position=4000,4000'],
});
// A fresh profile downloads the catalogued lists at startup. Navigating
// before they arrive measures the compiled-in baseline alone, which would
// understate the blocker and misattribute the leaks.
const warm = await browser.newPage();
await warm.goto('about:blank');
process.stdout.write('waiting for filter lists');
for (let i = 0; i < 12; i++) {
  await new Promise(r => setTimeout(r, 2500));
  process.stdout.write('.');
}
console.log('');
await warm.close();

for (const site of sites) {
  const page = await browser.newPage();
  let attempted = 0, blocked = 0, through = 0, frames = 0;
  page.on('request', r => { if (AD_HOSTS.test(r.url())) attempted++; });
  page.on('requestfailed', r => {
    if (!AD_HOSTS.test(r.url())) return;
    const t = r.failure()?.errorText ?? '';
    if (/BLOCKED_BY_CLIENT|ERR_BLOCKED/.test(t)) blocked++; else through++;
  });
  const leaked = new Map();
  page.on('response', r => {
    if (!AD_HOSTS.test(r.url()) || r.status() >= 400) return;
    through++;
    const h = new URL(r.url()).host;
    leaked.set(h, (leaked.get(h) ?? 0) + 1);
  });
  try {
    await page.goto(site, {waitUntil: 'networkidle2', timeout: 45000});
    await new Promise(r => setTimeout(r, 3000));
    frames = page.frames().length;
  } catch (e) { console.log(`  ${site}: navigation issue - ${e.message.split('\n')[0]}`); }
  const pct = attempted ? Math.round(100 * blocked / attempted) : 0;
  console.log(`${site.padEnd(34)} attempted ${String(attempted).padStart(3)}  blocked ${String(blocked).padStart(3)} (${pct}%)  through ${String(through).padStart(3)}  frames ${frames}`);
  for (const [h, n] of [...leaked].sort((a, b) => b[1] - a[1]).slice(0, 6)) {
    console.log(`      through: ${h} x${n}`);
  }
  await page.close();
}
await browser.close();
