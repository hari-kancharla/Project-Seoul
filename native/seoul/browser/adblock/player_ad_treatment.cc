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
  window.__seoulPlayerAdTreatment = true;

  const PLAYERS = [
    {
      ad: '.html5-video-player.ad-showing',
      scope: '.html5-video-player.ad-showing',
      skip: ['.ytp-skip-ad-button', '.ytp-ad-skip-button',
             '.ytp-ad-skip-button-modern', '.ytp-ad-survey-answer-button'],
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

  const treat = () => {
    for (const player of PLAYERS) {
      for (const root of document.querySelectorAll(player.ad)) {
        // Seek the ad's own media to its end. Scoped to the ad container so a
        // content video is never touched; SSAI streams present no such
        // element, which is the honest boundary of this treatment.
        const scope =
            root.matches(player.scope) ? root : root.querySelector(player.scope);
        const video = scope ? scope.querySelector('video') : null;
        if (video && Number.isFinite(video.duration) &&
            video.duration > 0.1 && video.currentTime < video.duration - 0.1) {
          try {
            video.muted = true;
            video.currentTime = video.duration;
          } catch (e) {}
        }
        for (const selector of player.skip) {
          for (const button of root.querySelectorAll(selector)) {
            try { button.click(); } catch (e) {}
          }
        }
      }
    }
  };

  new MutationObserver(treat).observe(
      document.documentElement,
      {subtree: true, attributes: true, attributeFilter: ['class']});
  setInterval(treat, 500);
  if (document.readyState !== 'loading') {
    treat();
  } else {
    document.addEventListener('DOMContentLoaded', treat, {once: true});
  }
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
