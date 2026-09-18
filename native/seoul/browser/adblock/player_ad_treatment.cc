// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/adblock/player_ad_treatment.h"

#include "url/gurl.h"

namespace seoul::adblock {

namespace {

// One engine, a table of players.
//
// A row names how a player's markup announces an ad (`ad`), where the ad's own
// media element lives (`scope` - seek only inside it, never the content
// video), and the player's skip controls (`skip`). Adding a player is adding a
// row; the behavior below never changes per site.
//
// Rows cover the player SDKs that serve the overwhelming share of client-side
// stitched web video ads:
//   - YouTube's HTML5 player (youtube.com, embeds)
//   - Google IMA, the SDK behind most publishers' prerolls (Crunchyroll-class
//     sites, news sites, sports sites)
//   - JW Player's ads plugin
//   - video.js ad integrations (contrib-ads convention)
constexpr char kPlayerAdTreatment[] = R"js(
(() => {
  if (window.__seoulPlayerAdTreatment) { return; }


  const PLAYERS = [
    {
      ad: '.html5-video-player.ad-showing',
      scope: '.html5-video-player.ad-showing',
      skip: ['.ytp-skip-ad-button', '.ytp-ad-skip-button',
             '.ytp-ad-skip-button-modern'],
    },
    {
      ad: '.ima-ad-container',
      scope: '.ima-ad-container',
      skip: ['.videoAdUiSkipButton', '.videoAdUiSkipContainer button'],
    },
    {
      ad: '.jw-flag-ads',
      scope: '.jw-flag-ads',
      skip: ['.jw-skip', '.jw-skippable'],
    },
    {
      ad: '.vjs-ad-playing',
      scope: '.vjs-ad-playing',
      skip: ['.vjs-skip-button', '.vjs-overlay-skip'],
    },
  ];

  const adjusted = new Map();
  const pressed = new WeakSet();
  const rootsSelector = PLAYERS.map(player => player.ad).join(',');
  let pending = false;
  let stopped = false;
  let timer = 0;
  const visible = element => element.getClientRects().length > 0 &&
      getComputedStyle(element).visibility !== 'hidden' &&
      getComputedStyle(element).display !== 'none';
  const restore = video => {
    const prior = adjusted.get(video);
    if (!prior) return;
    // Restore only the property this fallback changed, preserving volume and
    // playback rate. Do not unmute a user-muted video that was already muted.
    if (video.muted) video.muted = prior.muted;
    adjusted.delete(video);
  };
  const treat = () => {
    pending = false;
    if (stopped) return;
    clearTimeout(timer);
    const active = new Set();
    let hasAd = false;
    for (const player of PLAYERS) {
      for (const root of [...document.querySelectorAll(player.ad)].slice(0, 32)) {
        if (!visible(root)) continue;
        hasAd = true;
        const scope = root.matches(player.scope) ? root : root.querySelector(player.scope);
        const video = scope ? scope.querySelector('video') : null;
        if (video) active.add(video);
        if (video && Number.isFinite(video.duration) && video.duration > 0.1 &&
            video.currentTime < video.duration - 0.1) {
          try {
            if (!adjusted.has(video)) adjusted.set(video, {muted: video.muted});
            video.muted = true;
            video.currentTime = video.duration;
          } catch {}
        }
        for (const selector of player.skip) {
          for (const button of [...root.querySelectorAll(selector)].slice(0, 8)) {
            if (pressed.has(button) || button.disabled || !visible(button)) continue;
            pressed.add(button);
            try { button.click(); } catch {}
          }
        }
      }
    }
    for (const video of adjusted.keys()) if (!active.has(video)) restore(video);
    // Media events and DOM insertion handle the normal path immediately. This
    // bounded fallback runs only while an ad is present, never on idle pages.
    if (hasAd) timer = setTimeout(treat, 1000);
  };
  const schedule = () => {
    if (pending || stopped) return;
    pending = true;
    queueMicrotask(treat);
  };
  const observer = new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes' || record.addedNodes.length || record.removedNodes.length) {
        schedule(); break;
      }
    }
  });
  observer.observe(document.documentElement, {
    subtree: true, childList: true, attributes: true, attributeFilter: ['class', 'hidden', 'disabled']
  });
  const mediaEvent = event => {
    if (event.target instanceof HTMLMediaElement &&
        (adjusted.has(event.target) || event.target.closest(rootsSelector))) schedule();
  };
  const events = ['loadedmetadata', 'durationchange', 'emptied', 'ended'];
  for (const event of events) document.addEventListener(event, mediaEvent, true);
  window.__seoulPlayerAdTreatment = {
    stop() {
      stopped = true;
      clearTimeout(timer);
      observer.disconnect();
      for (const event of events) document.removeEventListener(event, mediaEvent, true);
      for (const video of adjusted.keys()) restore(video);
      delete window.__seoulPlayerAdTreatment;
    }
  };
  treat();
})();
)js";

}  // namespace

std::string PlayerAdTreatmentScriptFor(const GURL& document_url) {
  if (!document_url.is_valid() || !document_url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  // Every http(s) document. The script does nothing until a pattern matches,
  // and the patterns are player SDKs rather than sites, so there is no site
  // list to fall behind.
  return kPlayerAdTreatment;
}

}  // namespace seoul::adblock
