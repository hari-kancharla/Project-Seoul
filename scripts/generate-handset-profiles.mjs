#!/usr/bin/env node
// Generate the Handset device catalog from Chromium's own emulated-device
// list, so the picker tracks real phones without anyone maintaining a table
// by hand. DevTools' EmulatedDevices.ts is Google's production catalog - it
// carries every device Chrome DevTools emulates, marked between explicit
// DEVICE-LIST-BEGIN/END comments precisely so scripts can extract it
// (chromedriver's embed_mobile_devices_in_cpp.py reads the same markers).
// Every Chromium roll that adds a phone therefore adds it here on the next
// regeneration, and check:handset-profiles fails CI until that happens.
//
// The overlay (handset_profiles_overlay.json) exists for devices newer than
// the pinned Chromium's catalog. Overlay entries win title collisions so
// their stable ids survive upstream catching up; the generator prints a note
// when upstream now carries an overlaid title, so the entry can be retired
// deliberately rather than silently.
//
// Featured selection is computed, not curated: the newest numbered device of
// each phone family (iPhone N, Pixel N, Galaxy S N) joins anything the
// overlay marks featured. When a Chromium roll brings iPhone <N+1>, it
// replaces iPhone <N> on the menu with no edit anywhere.
//
//   node scripts/generate-handset-profiles.mjs           # regenerate
//   node scripts/generate-handset-profiles.mjs --check   # CI drift gate

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
const lock = JSON.parse(readFileSync(path.join(repo, 'native/chromium.lock.json'), 'utf8'));

// The checkout location rule lives in one place (checkout-root.mjs); reuse it.
const { checkoutSrc } = await import(path.join(repo, 'native/scripts/checkout-root.mjs'));
const src = checkoutSrc();

const catalogPath = path.join(
    src, 'third_party/devtools-frontend/src/front_end/models/emulation/EmulatedDevices.ts');
const overlayPath = path.join(repo, 'native/seoul/browser/handset/handset_profiles_overlay.json');
const outCcPath = path.join(repo, 'native/seoul/browser/handset/handset_profiles_generated.cc');
const outHPath = path.join(repo, 'native/seoul/browser/handset/handset_profiles_generated.h');

// Bounds mirrored from handset_types.h - an entry outside them cannot be a
// Handset and is skipped with a note rather than clamped into a lie.
const BOUNDS = { minW: 240, maxW: 1366, minH: 320, maxH: 1600, minDpr: 1.0, maxDpr: 4.0 };

function fail(msg) { console.error('generate-handset-profiles: ' + msg); process.exit(1); }

// --- 1. Extract the catalog array from the TS source -------------------------
if (!existsSync(catalogPath)) fail('no DevTools catalog at ' + catalogPath);
const ts = readFileSync(catalogPath, 'utf8');
const begin = ts.indexOf('// DEVICE-LIST-BEGIN');
const end = ts.indexOf('// DEVICE-LIST-END');
if (begin < 0 || end < 0 || end <= begin) fail('DEVICE-LIST markers not found; upstream layout changed');
const body = ts.slice(ts.indexOf('\n', begin) + 1, ts.lastIndexOf('\n', end));
let rawDevices;
try {
  // The catalog wraps some titles in DevTools' lazy-i18n helper; identity is
  // the right stub since the wrapped value IS the source string.
  const sandbox = Object.create(null);
  sandbox.i18nLazyString = (s) => s;
  sandbox.i18n = { i18n: { lockedLazyString: (s) => s } };
  // Only the laptop entries title through UIStrings, and those are filtered
  // by type anyway - the property name is a fine stand-in.
  sandbox.UIStrings = new Proxy({}, { get: (_, prop) => String(prop) });
  rawDevices = vm.runInNewContext('[' + body + ']', sandbox, { timeout: 5000 });
} catch (e) {
  fail('catalog slice did not evaluate as an array: ' + e.message);
}
if (!Array.isArray(rawDevices) || rawDevices.length < 20) {
  fail('catalog parsed but looks wrong (' + (rawDevices?.length ?? 'not an array') + ' entries)');
}

// --- 2. Filter and normalise -------------------------------------------------
const slug = (title) => title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
const skipped = [];
const catalog = [];
for (const d of rawDevices) {
  const meta = d['user-agent-metadata'] ?? {};
  const type = d['type'];
  const platform = meta['platform'];
  const caps = d['capabilities'] ?? [];
  const vertical = d['screen']?.['vertical'];
  const dpr = d['screen']?.['device-pixel-ratio'];
  const modes = d['modes'];
  const reasonSkip = (why) => skipped.push(`${d['title']} (${why})`);

  if (type !== 'phone' && type !== 'tablet') { reasonSkip('type ' + type); continue; }
  if (platform !== 'iOS' && platform !== 'Android') { reasonSkip('platform ' + platform); continue; }
  if (!caps.includes('touch') || !caps.includes('mobile')) { reasonSkip('capabilities'); continue; }
  // A device whose modes never include a vertical orientation (Nest Hubs)
  // cannot hold the portrait presentation Handset opens with.
  if (Array.isArray(modes) && !modes.some((m) => String(m['orientation']).includes('vertical'))) {
    reasonSkip('no portrait mode'); continue;
  }
  if (!vertical || !dpr) { reasonSkip('no screen metrics'); continue; }
  // Seoul's UA builder reproduces stock Safari-on-iOS and Chrome-on-Android.
  // A catalog entry defined by a different browser's UA (Facebook's in-app
  // browser, Chrome-on-iOS) would emulate the screen while lying about the
  // browser, so it is excluded rather than half-emulated. Stock shapes in
  // the catalog: Android Chrome carries a %s version placeholder; iPads
  // report either the mobile Safari UA or - since iPadOS 13 - the
  // desktop-Mac Safari UA, and both are stock Safari.
  const ua = String(d['user-agent'] ?? '');
  const stock = platform === 'iOS'
      ? (/Version\/[\d.]+ (Mobile\/\S+ )?Safari/.test(ua) &&
         !/CriOS|FxiOS|GSA|FBAN/.test(ua))
      : (/Chrome\/(%s|[\d.]+)/.test(ua) &&
         !/FB_IAB|FBAN|EdgA|OPR\/|SamsungBrowser|UCBrowser/.test(ua));
  if (!stock) { reasonSkip('non-stock browser UA'); continue; }
  // Portrait means width < height by definition. Upstream's list is not
  // guaranteed to agree - the Lumia 550 entry records its vertical screen as
  // 640x360 - so normalize rather than inherit the quirk.
  const width = Math.min(vertical.width, vertical.height);
  const height = Math.max(vertical.width, vertical.height);
  if (width < BOUNDS.minW || width > BOUNDS.maxW || height < BOUNDS.minH || height > BOUNDS.maxH ||
      dpr < BOUNDS.minDpr || dpr > BOUNDS.maxDpr) {
    reasonSkip(`out of bounds ${width}x${height}@${dpr}`); continue;
  }
  catalog.push({
    id: slug(d['title']),
    label: d['title'],
    platform,
    form_factor: type,
    width, height,
    dpr,
    // iOS versions are underscored in the UA CPU clause; the builder converts
    // back to dotted where Safari reports dotted.
    platform_version: platform === 'iOS'
        ? String(meta['platformVersion'] ?? '17_0').replace(/\./g, '_')
        : String(meta['platformVersion'] ?? '13'),
    model: platform === 'Android' ? String(meta['model'] ?? d['title']) : '',
  });
}

// --- 3. Merge the overlay (overlay wins on title) ----------------------------
const overlay = JSON.parse(readFileSync(overlayPath, 'utf8'));
const byTitle = new Map(catalog.map((p) => [p.label, p]));
const notes = [];
for (const o of overlay.devices) {
  if (byTitle.has(o.label)) {
    notes.push(`upstream now carries "${o.label}" - overlay keeps id continuity; ` +
               'compare metrics and retire the overlay entry when they agree');
  }
  byTitle.set(o.label, { ...o });
}
let profiles = [...byTitle.values()];

// Uniqueness is a hard contract: ids persist in prefs.
const ids = new Set();
for (const p of profiles) {
  if (ids.has(p.id)) fail('duplicate id ' + p.id);
  ids.add(p.id);
}

// --- 4. Featured: overlay flags + newest numbered device per phone family ----
// Vendor prefixes vary between the catalog and common usage ("Samsung Galaxy
// S20" vs "Galaxy S23"); strip them so one family is one family, or the
// newest-wins rule would feature two generations of the same line.
const familyLabel = (label) => label.replace(/^Samsung /, '');
const FAMILIES = [/^iPhone (\d+)/, /^Pixel (\d+)/, /^Galaxy S(\d+)/];
const featured = new Set(overlay.devices.filter((o) => o.featured).map((o) => o.id));
for (const family of FAMILIES) {
  let best = null;
  let bestN = -1;
  for (const p of profiles) {
    const m = familyLabel(p.label).match(family);
    if (m && Number(m[1]) > bestN) { bestN = Number(m[1]); best = p; }
  }
  if (best) featured.add(best.id);
}

// Stable order: phones before tablets, featured first within each. Featured
// entries run narrow-to-wide - the picker reads as a size ramp, which is the
// choice being made - while the long tail sorts by label for scanning.
const rank = (p) => (p.form_factor === 'phone' ? 0 : 1) * 2 + (featured.has(p.id) ? 0 : 1);
profiles.sort((a, b) =>
  rank(a) - rank(b) ||
  (featured.has(a.id) ? a.width - b.width : a.label.localeCompare(b.label)) ||
  a.label.localeCompare(b.label));

// --- 5. Emit -----------------------------------------------------------------
const esc = (s) => s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const enumPlatform = (p) => p === 'iOS' ? 'HandsetPlatform::kIOS' : 'HandsetPlatform::kAndroid';
const enumForm = (f) => f === 'tablet' ? 'HandsetFormFactor::kTablet' : 'HandsetFormFactor::kPhone';
const fmtDpr = (d) => Number.isInteger(d) ? d + '.0f' : d + 'f';

const rows = profiles.map((p) =>
  `          {"${esc(p.id)}", "${esc(p.label)}", ${enumPlatform(p.platform)},\n` +
  `           ${enumForm(p.form_factor)}, ${p.width}, ${p.height}, ${fmtDpr(p.dpr)},\n` +
  `           "${esc(p.platform_version)}", "${esc(p.model)}"},`).join('\n');
const featuredRows = profiles.filter((p) => featured.has(p.id))
  .map((p) => `          "${esc(p.id)}",`).join('\n');

const banner =
`// GENERATED by scripts/generate-handset-profiles.mjs - do not edit.
// Source: DevTools emulated-device catalog in the pinned Chromium checkout
// (${lock.chromium.revision.slice(0, 12)}) merged with
// native/seoul/browser/handset/handset_profiles_overlay.json.
// Regenerate: npm run generate:handset   Gate: npm run check:handset-profiles`;

const hOut = `${banner}

#ifndef SEOUL_BROWSER_HANDSET_HANDSET_PROFILES_GENERATED_H_
#define SEOUL_BROWSER_HANDSET_HANDSET_PROFILES_GENERATED_H_

#include <string>
#include <vector>

#include "seoul/browser/handset/handset_types.h"

namespace seoul {

// Every device this build can emulate, phones first, featured first.
const std::vector<HandsetProfile>& GeneratedHandsetProfiles();

// Ids of the profiles the picker shows at top level; the rest live under
// "All devices". Computed at generation time - see the generator header.
const std::vector<std::string>& GeneratedFeaturedHandsetProfileIds();

}  // namespace seoul

#endif  // SEOUL_BROWSER_HANDSET_HANDSET_PROFILES_GENERATED_H_
`;

const ccOut = `${banner}

#include "seoul/browser/handset/handset_profiles_generated.h"

#include "base/no_destructor.h"

namespace seoul {

const std::vector<HandsetProfile>& GeneratedHandsetProfiles() {
  static const base::NoDestructor<const std::vector<HandsetProfile>> profiles(
      std::vector<HandsetProfile>{
${rows}
      });
  return *profiles;
}

const std::vector<std::string>& GeneratedFeaturedHandsetProfileIds() {
  static const base::NoDestructor<const std::vector<std::string>> ids(
      std::vector<std::string>{
${featuredRows}
      });
  return *ids;
}

}  // namespace seoul
`;

const check = process.argv.includes('--check');
if (check) {
  const currentCc = existsSync(outCcPath) ? readFileSync(outCcPath, 'utf8') : '';
  const currentH = existsSync(outHPath) ? readFileSync(outHPath, 'utf8') : '';
  if (currentCc !== ccOut || currentH !== hOut) {
    fail('generated handset profiles are stale for this checkout - run: npm run generate:handset');
  }
  console.log(`handset-profiles: OK (${profiles.length} devices, ${featured.size} featured, ` +
              `${skipped.length} catalog entries filtered)`);
} else {
  writeFileSync(outCcPath, ccOut);
  writeFileSync(outHPath, hOut);
  console.log(`wrote ${profiles.length} devices (${featured.size} featured) from ` +
              `${catalog.length} catalog + ${overlay.devices.length} overlay entries`);
  for (const n of notes) console.log('note: ' + n);
  if (skipped.length) console.log('filtered: ' + skipped.join(', '));
}
