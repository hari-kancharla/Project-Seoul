// Project Seoul URL de-tracking.
//
// Strips the click-identifier parameters that carry a person between sites -
// utm_*, fbclid, gclid and their relatives - from URLs the blocker is already
// evaluating. These do nothing for the page: they exist so the sending site and
// the receiving site can agree that this is the same person, which is the same
// cross-site correlation the blocker's network rules exist to break, arriving
// in the address bar instead of in a request.
//
// Filter lists express this with $removeparam, but Seoul's two-engine policy
// neutralises rewrites from the default engine, so no catalogued rule reaches
// production. This is the browser's own bounded table, applied beside the
// engine rather than instead of it: a list rule always wins, and this only runs
// when the engine asked for nothing.
//
// Deliberately a fixed table rather than a heuristic. Guessing that a parameter
// "looks like tracking" would eventually strip a parameter a site needs, and a
// page that silently loses state is worse than a tracker that survives.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_URL_DETRACKER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_URL_DETRACKER_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace seoul::adblock {

// True when `name` is a known cross-site click identifier.
bool IsTrackingQueryParameter(std::string_view name);

// `url` with every tracking parameter removed, or nullopt when it carries none.
//
// Parameters that stay keep their exact original spelling and escaping: the
// query is rebuilt from the untouched raw segments, never re-encoded, so the
// result differs from the input only by whole removed pairs. That is precisely
// the invariant IsSafeAdBlockUrlRewrite re-checks before the rewrite is used.
std::optional<GURL> RemoveTrackingParameters(const GURL& url);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_URL_DETRACKER_H_
