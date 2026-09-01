// Project Seoul URL de-tracking.

#include "seoul/browser/adblock/ad_block_url_detracker.h"

#include <algorithm>
#include <array>
#include <vector>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace seoul::adblock {

namespace {

// Exact parameter names. Sorted only for readability; lookup is linear over a
// table this size and a query this short.
constexpr std::array<std::string_view, 34> kTrackingParameters = {
    // Google Ads and Analytics click identifiers.
    "dclid", "gbraid", "gclid", "gclsrc", "wbraid",
    // Meta.
    "fbclid", "fb_action_ids", "fb_action_types", "fb_source", "fb_ref",
    // Microsoft, Yandex, Mailchimp, HubSpot, Marketo, Klaviyo, Olytics.
    "msclkid", "yclid", "ysclid", "mc_cid", "mc_eid", "_hsenc", "_hsmi",
    "hsctatracking", "mkt_tok", "_kx", "vero_conv", "vero_id",
    // Social and content platforms.
    "igshid", "igsh", "twclid", "ttclid", "li_fat_id", "epik", "rb_clickid",
    "s_kwcid", "irclickid",
    // Marketplaces and shorteners.
    "spm", "scm", "_openstat",
};

// Prefixes. `utm_` covers the whole Urchin family, which is open-ended, and
// naming each member would fall behind.
constexpr std::array<std::string_view, 3> kTrackingPrefixes = {
    "utm_",
    "pk_",   // Matomo's equivalent of utm_.
    "mtm_",  // Matomo's newer spelling.
};

}  // namespace

bool IsTrackingQueryParameter(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  for (std::string_view prefix : kTrackingPrefixes) {
    if (base::StartsWith(name, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return std::ranges::any_of(
      kTrackingParameters, [name](std::string_view known) {
        return base::EqualsCaseInsensitiveASCII(name, known);
      });
}

std::optional<GURL> RemoveTrackingParameters(const GURL& url) {
  if (!url.is_valid() || !url.has_query()) {
    return std::nullopt;
  }
  const std::string_view query = url.query();
  std::vector<std::string_view> kept;
  bool removed_any = false;
  // Walk the raw query and keep whole segments verbatim. Splitting on '=' only
  // to read the name means a kept value is never re-encoded, so a parameter
  // carrying an already-escaped payload survives byte for byte.
  for (std::string_view segment : base::SplitStringPiece(
           query, "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const size_t equals = segment.find('=');
    const std::string_view name =
        equals == std::string_view::npos ? segment : segment.substr(0, equals);
    if (IsTrackingQueryParameter(name)) {
      removed_any = true;
      continue;
    }
    kept.push_back(segment);
  }
  if (!removed_any) {
    return std::nullopt;
  }

  GURL::Replacements replacements;
  const std::string remaining = base::JoinString(kept, "&");
  if (remaining.empty()) {
    // Every parameter was tracking. Clear the query rather than leaving a bare
    // '?', which some servers treat differently from no query at all.
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(remaining);
  }
  GURL result = url.ReplaceComponents(replacements);
  if (!result.is_valid()) {
    return std::nullopt;
  }
  return result;
}

}  // namespace seoul::adblock
