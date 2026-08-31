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

#include "base/strings/strcat.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"

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

  // The rest of uBlock Origin's own filter set.
  //
  // `filters.txt` above is only the CURRENT working file; uBlock's engine is
  // the whole uAssets set, and the generic network rules most sites need live
  // in `filters-general.txt` rather than in the working file. Subscribing to
  // one of the fourteen and calling it "uBlock Origin filters" overstated the
  // coverage by roughly five to one by size. Brave ships the equivalent set.
  //
  // All of them go in the DEFAULT engine, including the exception files:
  // `unbreak.txt` and `quick-fixes.txt` exist to cancel blocks made by their
  // siblings, so separating them into the additional engine would change what
  // they cancel. Bounds are generous against each file's measured size so a
  // list that grows between releases is not silently rejected.
  const auto ublock_file = [&catalog](const char* id_suffix, const char* file,
                                      const char* what) {
    catalog.push_back(MakeEntry(
        base::StrCat({"ublock-", id_suffix}),
        base::StrCat({"uBlock Origin: ", what}), "uBlock Origin",
        base::StrCat(
            {"https://ublockorigin.github.io/uAssetsCDN/filters/", file}),
        "GPL-3.0-only", "Raymond Hill and uAssets contributors",
        AdBlockListDelivery::kRuntimeDownload, /*enabled_by_default=*/true,
        AdBlockEngineGroup::kDefault, 12u * kMiB, 24));
  };
  ublock_file("general", "filters-general.txt",
              "general network and cosmetic rules");
  ublock_file("unbreak", "unbreak.txt",
              "exceptions that repair sites the other files break");
  ublock_file("badware", "badware.txt",
              "badware and malvertising hosts");
  ublock_file("quick-fixes", "quick-fixes.txt",
              "urgent repairs published between releases");
  ublock_file("resource-abuse", "resource-abuse.txt",
              "cryptomining and other resource abuse");
  ublock_file("link-shorteners", "ubo-link-shorteners.txt",
              "tracking link shorteners");
  ublock_file("2020", "filters-2020.txt",
              "rules retired from the working file in 2020");
  ublock_file("2021", "filters-2021.txt",
              "rules retired from the working file in 2021");
  ublock_file("2022", "filters-2022.txt",
              "rules retired from the working file in 2022");
  ublock_file("2023", "filters-2023.txt",
              "rules retired from the working file in 2023");
  ublock_file("2024", "filters-2024.txt",
              "rules retired from the working file in 2024");
  ublock_file("2025", "filters-2025.txt",
              "rules retired from the working file in 2025");
  ublock_file("2026", "filters-2026.txt",
              "rules retired from the working file in 2026");

  // Brave's site-compatibility ("unbreak") rules. The brave/adblock-lists
  // repository is MPL-2.0 and states that individual lists may carry their own
  // upstream licences, so only Brave's own unbreak list is catalogued here
  // rather than the aggregated feeds that repository republishes.
  //
  // Enabled by default, exactly as Brave ships it: this list is exceptions
  // that repair sites the blocking lists break, so leaving it off means
  // shipping the breakage without the repair - and the catalogue subscriber
  // only ever downloads default-enabled entries, so `false` here made the
  // entry dead configuration. It stays in the additional engine, whose
  // exceptions suppress default-engine blocks (the evaluator feeds the
  // default engine's match into the additional pass).
  catalog.push_back(MakeEntry(
      "brave-unbreak", "Brave site compatibility", "Brave Software",
      "https://raw.githubusercontent.com/brave/adblock-lists/master/"
      "brave-lists/brave-unbreak.txt",
      "MPL-2.0", "Brave Software", AdBlockListDelivery::kRuntimeDownload,
      /*enabled_by_default=*/true, AdBlockEngineGroup::kAdditional,
      4u * kMiB, 24));

  // Regional lists, auto-selected by profile language rather than by a
  // buried toggle - Brave's regional-catalog behaviour. Every entry is the
  // language community's own canonical EasyList-family list from the
  // project's canonical host, under the same GPL-3.0-or-later OR CC-BY-SA-3.0
  // terms as EasyList itself (EasyList Hebrew is served from the EasyList
  // organisation's own repository). They join the default engine because they
  // are the same vetted blocking tier as EasyList - only their activation is
  // conditional, never their trust.
  const auto regional = [&catalog](std::string id, std::string name,
                                   std::string url,
                                   std::vector<std::string> languages) {
    AdBlockCatalogEntry entry = MakeEntry(
        std::move(id), std::move(name), "EasyList", std::move(url),
        "GPL-3.0-or-later OR CC-BY-SA-3.0", "The EasyList authors",
        AdBlockListDelivery::kRuntimeDownload, /*enabled_by_default=*/false,
        AdBlockEngineGroup::kDefault, 8u * kMiB, 24);
    entry.languages = std::move(languages);
    catalog.push_back(std::move(entry));
  };
  regional("easylist-germany", "EasyList Germany",
           "https://easylist.to/easylistgermany/easylistgermany.txt", {"de"});
  regional("liste-fr", "Liste FR",
           "https://easylist-downloads.adblockplus.org/liste_fr.txt", {"fr"});
  regional("easylist-italy", "EasyList Italy",
           "https://easylist-downloads.adblockplus.org/easylistitaly.txt",
           {"it"});
  regional("easylist-spanish", "EasyList Spanish",
           "https://easylist-downloads.adblockplus.org/easylistspanish.txt",
           {"es"});
  regional("easylist-dutch", "EasyList Dutch",
           "https://easylist-downloads.adblockplus.org/easylistdutch.txt",
           {"nl"});
  regional("easylist-portuguese", "EasyList Portuguese",
           "https://easylist-downloads.adblockplus.org/easylistportuguese.txt",
           {"pt"});
  regional("easylist-polish", "EasyList Polish",
           "https://easylist-downloads.adblockplus.org/easylistpolish.txt",
           {"pl"});
  regional("ruadlist", "RU AdList",
           "https://easylist-downloads.adblockplus.org/advblock.txt",
           {"ru", "uk", "be"});
  regional("easylist-china", "EasyList China",
           "https://easylist-downloads.adblockplus.org/easylistchina.txt",
           {"zh"});
  regional("abpindo", "ABPindo",
           "https://easylist-downloads.adblockplus.org/abpindo.txt", {"id"});
  regional("liste-ar", "Liste AR",
           "https://easylist-downloads.adblockplus.org/Liste_AR.txt", {"ar"});
  regional("easylist-hebrew", "EasyList Hebrew",
           "https://raw.githubusercontent.com/easylist/EasyListHebrew/master/"
           "EasyListHebrew.txt",
           {"he"});

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

std::vector<std::string> NormalizeCatalogLanguages(
    const std::vector<std::string>& raw) {
  std::vector<std::string> out;
  for (const std::string& tag : raw) {
    std::string language;
    for (char c : tag) {
      if (c == '-' || c == '_') {
        break;
      }
      language.push_back(base::ToLowerASCII(c));
    }
    // Two- or three-letter primary subtags only; anything else is not a
    // language and must not accidentally match a catalog entry.
    if (language.size() < 2 || language.size() > 3 ||
        !std::ranges::all_of(language, base::IsAsciiLower<char>)) {
      continue;
    }
    if (std::ranges::find(out, language) == out.end()) {
      out.push_back(language);
    }
  }
  return out;
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
