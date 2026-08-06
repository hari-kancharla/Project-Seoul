// Project Seoul production filter-list catalog tests.

#include "seoul/browser/adblock/ad_block_filter_catalog.h"

#include <set>
#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace seoul::adblock {
namespace {

TEST(AdBlockFilterCatalogTest, IdsAreUniqueAndStable) {
  std::set<std::string> ids;
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    EXPECT_FALSE(entry.id.empty());
    EXPECT_TRUE(ids.insert(entry.id).second) << "duplicate id: " << entry.id;
  }
  // Ids are persisted in prefs, so the shipped set is pinned here: renaming or
  // dropping one silently orphans a user's stored selection.
  EXPECT_TRUE(ids.contains("seoul-baseline"));
  EXPECT_TRUE(ids.contains("easylist"));
  EXPECT_TRUE(ids.contains("easyprivacy"));
}

TEST(AdBlockFilterCatalogTest, EveryEntryCarriesLicenceMetadata) {
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    EXPECT_FALSE(entry.license.empty()) << entry.id;
    EXPECT_FALSE(entry.name.empty()) << entry.id;
    EXPECT_FALSE(entry.project.empty()) << entry.id;
  }
}

// Third-party lists carry credit requirements; Seoul-owned content does not.
TEST(AdBlockFilterCatalogTest, ThirdPartyEntriesNameTheirAttribution) {
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    if (entry.project == "Project Seoul") {
      continue;
    }
    EXPECT_FALSE(entry.attribution.empty())
        << entry.id << " redistributes third-party rules without credit";
  }
}

// Anything not authored by Seoul is fetched from its upstream project rather
// than shipped inside the binary.
TEST(AdBlockFilterCatalogTest, OnlySeoulOwnedContentIsBundled) {
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    if (entry.delivery == AdBlockListDelivery::kBundled) {
      EXPECT_EQ(entry.project, "Project Seoul") << entry.id;
      EXPECT_TRUE(entry.url.empty()) << entry.id;
    } else {
      EXPECT_FALSE(entry.url.empty()) << entry.id;
    }
  }
}

// A downloaded list is remote input: it must be HTTPS and size-capped before it
// is ever parsed.
TEST(AdBlockFilterCatalogTest, DownloadedListsAreHttpsAndBounded) {
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    if (entry.delivery != AdBlockListDelivery::kRuntimeDownload) {
      continue;
    }
    const GURL url{entry.url};
    EXPECT_TRUE(url.is_valid()) << entry.id;
    EXPECT_TRUE(url.SchemeIs(url::kHttpsScheme))
        << entry.id << " must not fetch rules over plaintext";
    EXPECT_GT(entry.max_bytes, 0u) << entry.id;
    EXPECT_LE(entry.max_bytes, 32u * 1024u * 1024u) << entry.id;
    EXPECT_GT(entry.update_interval_hours, 0) << entry.id;
  }
}

// The baseline must stay on by default and in the default engine: it is the
// floor a failed update falls back to.
TEST(AdBlockFilterCatalogTest, BundledBaselineIsDefaultEnabled) {
  const std::optional<AdBlockCatalogEntry> baseline =
      FindAdBlockCatalogEntry("seoul-baseline");
  ASSERT_TRUE(baseline);
  EXPECT_TRUE(baseline->enabled_by_default);
  EXPECT_EQ(baseline->group, AdBlockEngineGroup::kDefault);
  EXPECT_EQ(baseline->delivery, AdBlockListDelivery::kBundled);
}

// Opt-in lists land in the additional engine so they cannot silently change
// the vetted default protection.
TEST(AdBlockFilterCatalogTest, OptionalListsUseTheAdditionalEngine) {
  for (const AdBlockCatalogEntry& entry : GetAdBlockFilterCatalog()) {
    if (!entry.enabled_by_default) {
      EXPECT_EQ(entry.group, AdBlockEngineGroup::kAdditional) << entry.id;
    }
  }
}

TEST(AdBlockFilterCatalogTest, LookupResolvesKnownAndRejectsUnknown) {
  const std::optional<AdBlockCatalogEntry> easylist =
      FindAdBlockCatalogEntry("easylist");
  ASSERT_TRUE(easylist);
  EXPECT_EQ(easylist->attribution, "The EasyList authors");
  EXPECT_EQ(easylist->license, "GPL-3.0-or-later OR CC-BY-SA-3.0");

  EXPECT_FALSE(FindAdBlockCatalogEntry("no-such-list").has_value());
  EXPECT_FALSE(FindAdBlockCatalogEntry("").has_value());
}

TEST(AdBlockFilterCatalogTest, DefaultEnabledIdsMatchCatalogFlags) {
  const std::vector<std::string> ids = GetDefaultEnabledCatalogIds();
  EXPECT_FALSE(ids.empty());
  for (const std::string& id : ids) {
    const std::optional<AdBlockCatalogEntry> entry =
        FindAdBlockCatalogEntry(id);
    ASSERT_TRUE(entry) << id;
    EXPECT_TRUE(entry->enabled_by_default) << id;
  }
}

}  // namespace
}  // namespace seoul::adblock
