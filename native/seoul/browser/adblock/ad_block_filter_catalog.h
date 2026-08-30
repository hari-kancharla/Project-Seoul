// Project Seoul production filter-list catalog.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_CATALOG_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_CATALOG_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "seoul/browser/adblock/ad_block_engine_host.h"

namespace seoul::adblock {

// How Seoul is permitted, and how Seoul chooses, to ship a list's contents.
enum class AdBlockListDelivery {
  // Seoul owns the content outright and ships it inside the signed component.
  kBundled = 0,
  // Fetched over HTTPS at runtime rather than shipped in the binary. Used both
  // where redistribution carries obligations Seoul declines to place on the
  // shipped bundle, and where the upstream project is the authoritative source.
  kRuntimeDownload = 1,
};

struct AdBlockCatalogEntry {
  AdBlockCatalogEntry();
  AdBlockCatalogEntry(const AdBlockCatalogEntry&);
  AdBlockCatalogEntry& operator=(const AdBlockCatalogEntry&);
  AdBlockCatalogEntry(AdBlockCatalogEntry&&);
  AdBlockCatalogEntry& operator=(AdBlockCatalogEntry&&);
  ~AdBlockCatalogEntry();

  // Stable identifier. Persisted in prefs and reported to the UI, so it must
  // never change once shipped.
  std::string id;
  std::string name;
  std::string project;
  // Canonical upstream URL. Empty for Seoul-owned bundled content.
  std::string url;
  // SPDX expression. `OR` means the recipient may choose either license.
  std::string license;
  // Required credit line, empty when the license imposes none.
  std::string attribution;
  AdBlockListDelivery delivery = AdBlockListDelivery::kRuntimeDownload;
  bool enabled_by_default = false;
  AdBlockEngineGroup group = AdBlockEngineGroup::kAdditional;
  // Hard ceiling on a downloaded body. A response exceeding this is discarded
  // before parsing, so a hostile or corrupt source cannot exhaust memory.
  size_t max_bytes = 0;
  int update_interval_hours = 24;
  // ISO 639-1 language codes this list serves. Empty means the list is
  // global. A non-empty set makes the entry a regional list: not enabled by
  // default, but auto-selected when any of the profile's languages match -
  // Brave's regional-catalog behaviour, driven by language rather than by a
  // toggle nobody finds.
  std::vector<std::string> languages;
};

// The full production catalog, in stable order.
std::vector<AdBlockCatalogEntry> GetAdBlockFilterCatalog();

std::optional<AdBlockCatalogEntry> FindAdBlockCatalogEntry(
    std::string_view id);

// Ids enabled on a fresh profile, in catalog order.
std::vector<std::string> GetDefaultEnabledCatalogIds();

// Lowercases and strips region subtags: {"de-AT", "PT_BR"} -> {"de", "pt"}.
// Empty and malformed tags drop out rather than matching anything.
std::vector<std::string> NormalizeCatalogLanguages(
    const std::vector<std::string>& raw);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_CATALOG_H_
