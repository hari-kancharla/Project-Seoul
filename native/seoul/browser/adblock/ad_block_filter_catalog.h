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
};

// The full production catalog, in stable order.
std::vector<AdBlockCatalogEntry> GetAdBlockFilterCatalog();

std::optional<AdBlockCatalogEntry> FindAdBlockCatalogEntry(
    std::string_view id);

// Ids enabled on a fresh profile, in catalog order.
std::vector<std::string> GetDefaultEnabledCatalogIds();

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_CATALOG_H_
