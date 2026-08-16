// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ADBLOCK_PLAYER_AD_TREATMENT_H_
#define SEOUL_BROWSER_ADBLOCK_PLAYER_AD_TREATMENT_H_

#include <string>

class GURL;

namespace seoul::adblock {

// The in-stream video-ad treatment, run in the renderer's isolated world
// alongside the cosmetic rules.
//
// Network rules cannot reach an ad stitched into the player, and CSS cannot
// skip one. What remains - and what this does - is reacting to the DOM the ad
// produces: when a player's own markup says an ad is showing, seek the ad's
// media element to its end and press the player's skip control. DOM and media
// state cross the isolated-world boundary; page JavaScript does not, which is
// why this is the strongest treatment Seoul's security model permits.
//
// It is one generic engine driven by a table of player patterns (YouTube's
// player, Google IMA, JW Player, video.js), not per-site code: recognising a
// player's ad state needs its markers the same way cosmetic filtering needs
// lists, but the mechanism never changes per site. The script is inert on
// pages where no pattern matches.
//
// Honest limits, so nobody upstream oversells this: ads stitched server-side
// into the content stream itself (Netflix's and Prime Video's ad tiers, and
// SSAI generally) present no ad-state DOM and no separate media element. No
// client-side blocker removes those, and this one does not claim to.
//
// Returns the script for `document_url`, or empty for non-http(s) documents.
// The caller applies the same vetting (size, UTF-8) as every isolated script.
std::string PlayerAdTreatmentScriptFor(const GURL& document_url);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_PLAYER_AD_TREATMENT_H_
