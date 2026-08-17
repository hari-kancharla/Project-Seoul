// Project Seoul production filter-list catalog.
//
// Licences below were read from each project's own published terms, not
// inferred from the fact that another browser ships the list. See
// docs/research/native-adblock-filter-licensing.md for the sources consulted
// and the reasoning behind each delivery decision.
//
// No list contents live in this repository. This file records *where* rules
// come from and *under what terms*; the rules themselves are either authored by
// Seoul and shipped in the signed component, or fetched from the upstream
// project at runtime.

#include "seoul/browser/adblock/ad_block_filter_catalog.h"

#include <algorithm>
#include <utility>

namespace seoul::adblock {
namespace {

constexpr size_t kMiB = 1024u * 1024u;

AdBlockCatalogEntry MakeEntry(std::string id,
                              std::string name,
                              std::string project,
                              std::string url,
                              std::string license,
                              std::string attribution,
                              AdBlockListDelivery delivery,
                              bool enabled_by_default,
                              AdBlockEngineGroup group,
                              size_t max_bytes,
                              int update_interval_hours) {
  AdBlockCatalogEntry entry;
  entry.id = std::move(id);
  entry.name = std::move(name);
  entry.project = std::move(project);
  entry.url = std::move(url);
  entry.license = std::move(license);
  entry.attribution = std::move(attribution);
  entry.delivery = delivery;
  entry.enabled_by_default = enabled_by_default;
  entry.group = group;
  entry.max_bytes = max_bytes;
  entry.update_interval_hours = update_interval_hours;
  return entry;
}

std::vector<AdBlockCatalogEntry> BuildCatalog() {
  std::vector<AdBlockCatalogEntry> catalog;

  // Seoul-authored safety baseline. Ships inside the signed component so a
  // first run with no network still blocks something, and so a failed update
  // always has a known-good floor to fall back to.
  catalog.push_back(MakeEntry(
      "seoul-baseline", "Seoul baseline protection", "Project Seoul",
      /*url=*/"", "MPL-2.0", /*attribution=*/"", AdBlockListDelivery::kBundled,
      /*enabled_by_default=*/true, AdBlockEngineGroup::kDefault, 2u * kMiB,
      24));

  // EasyList and EasyPrivacy are dual licensed GPL-3.0-or-later OR
  // CC-BY-SA-3.0. Redistribution is permitted under either, with credit to
  // "The EasyList authors" and a share-alike obligation. Seoul fetches them at
  // runtime rather than bundling: that keeps the share-alike and attribution
  // obligations off the shipped binary and keeps the upstream project the
  // authoritative source. A conservative choice, not a claim that bundling
  // would be disallowed.
  catalog.push_back(MakeEntry(
      "easylist", "EasyList", "EasyList",
      "https://easylist.to/easylist/easylist.txt",
      "GPL-3.0-or-later OR CC-BY-SA-3.0", "The EasyList authors",
      AdBlockListDelivery::kRuntimeDownload, /*enabled_by_default=*/true,
      AdBlockEngineGroup::kDefault, 12u * kMiB, 24));

  catalog.push_back(MakeEntry(
      "easyprivacy", "EasyPrivacy", "EasyList",
      "https://easylist.to/easylist/easyprivacy.txt",
      "GPL-3.0-or-later OR CC-BY-SA-3.0", "The EasyList authors",
      AdBlockListDelivery::kRuntimeDownload, /*enabled_by_default=*/true,
      AdBlockEngineGroup::kDefault, 12u * kMiB, 24));

  // uBlock Origin's own filters, GPL-3.0 per uBlockOrigin/uAssets LICENSE.
  // Enabled by default because EasyList alone does not touch the ad class a
  // user actually complains about first: YouTube's own sponsored cards and
  // overlays, served from the site's origin where network rules cannot reach
  // them. uBlock's filters carry the cosmetic rules that hide that class. In
  // the default engine its rewrite rules are neutralised by the two-engine
  // policy, which is the conservative reading of a browser-vetted list.
  catalog.push_back(MakeEntry(
      "ublock-filters", "uBlock Origin filters", "uBlock Origin",
      "https://ublockorigin.github.io/uAssetsCDN/filters/filters.txt",
      "GPL-3.0-only", "Raymond Hill and uAssets contributors",
      AdBlockListDelivery::kRuntimeDownload, /*enabled_by_default=*/true,
      AdBlockEngineGroup::kDefault, 12u * kMiB, 24));

  // Brave's site-compatibility ("unbreak") rules. The brave/adblock-lists
  // repository is MPL-2.0 and states that individual lists may carry their own
  // upstream licences, so only Brave's own unbreak list is catalogued here
  // rather than the aggregated feeds that repository republishes.
  catalog.push_back(MakeEntry(
      "brave-unbreak", "Brave site compatibility", "Brave Software",
      "https://raw.githubusercontent.com/brave/adblock-lists/master/"
      "brave-lists/brave-unbreak.txt",
      "MPL-2.0", "Brave Software", AdBlockListDelivery::kRuntimeDownload,
      /*enabled_by_default=*/false, AdBlockEngineGroup::kAdditional,
      4u * kMiB, 24));

  return catalog;
}

}  // namespace

AdBlockCatalogEntry::AdBlockCatalogEntry() = default;
AdBlockCatalogEntry::AdBlockCatalogEntry(const AdBlockCatalogEntry&) = default;
AdBlockCatalogEntry& AdBlockCatalogEntry::operator=(
    const AdBlockCatalogEntry&) = default;
AdBlockCatalogEntry::AdBlockCatalogEntry(AdBlockCatalogEntry&&) = default;
AdBlockCatalogEntry& AdBlockCatalogEntry::operator=(AdBlockCatalogEntry&&) =
    default;
AdBlockCatalogEntry::~AdBlockCatalogEntry() = default;

// Returned by value rather than held in a process-global: the catalogue is
// small, is read on configuration and UI paths rather than the request hot
// path, and Seoul forbids process-global mutable state.
std::vector<AdBlockCatalogEntry> GetAdBlockFilterCatalog() {
  return BuildCatalog();
}

std::optional<AdBlockCatalogEntry> FindAdBlockCatalogEntry(std::string_view id) {
  const std::vector<AdBlockCatalogEntry> catalog = GetAdBlockFilterCatalog();
  const auto it = std::ranges::find(catalog, id, &AdBlockCatalogEntry::id);
  if (it == catalog.end()) {
    return std::nullopt;
  }
  return *it;
}

std::vector<std::string> GetDefaultEnabledCatalogIds() {
  std::vector<std::string> ids;
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    if (entry.enabled_by_default) {
      ids.push_back(entry.id);
    }
  }
  return ids;
}

}  // namespace seoul::adblock
