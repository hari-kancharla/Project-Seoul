// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/adblock/ad_block_catalogue_subscriber.h"

#include <algorithm>
#include <string_view>

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

AdBlockCatalogEntry MakeEntry(std::string id,
                              std::string url,
                              bool enabled,
                              AdBlockListDelivery delivery,
                              int interval_hours = 24) {
  AdBlockCatalogEntry entry;
  entry.id = std::move(id);
  entry.url = std::move(url);
  entry.enabled_by_default = enabled;
  entry.delivery = delivery;
  entry.update_interval_hours = interval_hours;
  return entry;
}

class AdBlockCatalogueSubscriberTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

// The real catalog must actually select something. This is the case that would
// have caught the original defect: EasyList and EasyPrivacy were catalogued as
// enabled-by-default runtime downloads and nothing ever fetched them, so the
// browser blocked with its baseline alone.
TEST_F(AdBlockCatalogueSubscriberTest, ProductionCatalogSubscribesToRealLists) {
  const std::vector<AdBlockCatalogEntry> selected =
      AdBlockCatalogueSubscriber::SelectSubscribedEntries(
          GetAdBlockFilterCatalog(), /*normalized_languages=*/{});
  ASSERT_FALSE(selected.empty())
      << "the shipped catalog must subscribe to at least one upstream list";
  for (const AdBlockCatalogEntry& entry : selected) {
    EXPECT_TRUE(entry.enabled_by_default);
    EXPECT_EQ(AdBlockListDelivery::kRuntimeDownload, entry.delivery);
    EXPECT_EQ(0u, entry.url.rfind("https://", 0))
        << entry.id << " must be fetched over HTTPS";
  }

  // Pinned by id: EasyList alone leaves same-origin ad surfaces standing -
  // YouTube's sponsored cards most visibly - and uBlock's filters carry the
  // cosmetic rules that hide that class. Dropping any of these from the
  // default subscription quietly reintroduces the ads a user notices first.
  auto has = [&selected](std::string_view id) {
    return std::ranges::any_of(selected, [&id](const AdBlockCatalogEntry& e) {
      return e.id == id;
    });
  };
  EXPECT_TRUE(has("easylist"));
  EXPECT_TRUE(has("easyprivacy"));
  EXPECT_TRUE(has("ublock-filters"));
}

TEST_F(AdBlockCatalogueSubscriberTest, SelectionSkipsWhatItMustSkip) {
  const std::vector<AdBlockCatalogEntry> catalog = {
      MakeEntry("bundled", "", true, AdBlockListDelivery::kBundled),
      MakeEntry("opt-in", "https://example.test/a.txt", false,
                AdBlockListDelivery::kRuntimeDownload),
      MakeEntry("insecure", "http://example.test/b.txt", true,
                AdBlockListDelivery::kRuntimeDownload),
      MakeEntry("malformed", "not a url", true,
                AdBlockListDelivery::kRuntimeDownload),
      MakeEntry("good", "https://example.test/c.txt", true,
                AdBlockListDelivery::kRuntimeDownload),
  };
  const std::vector<AdBlockCatalogEntry> selected =
      AdBlockCatalogueSubscriber::SelectSubscribedEntries(
          catalog, /*normalized_languages=*/{});
  ASSERT_EQ(1u, selected.size());
  EXPECT_EQ("good", selected.front().id);
}

// Regional lists ride on the profile's languages, not on a toggle. A German
// profile also gets EasyList Germany; an English profile gets exactly the
// global set; a multilingual profile gets every one of its communities'
// lists; and the region subtag never matters.
TEST_F(AdBlockCatalogueSubscriberTest, RegionalListsFollowProfileLanguages) {
  const std::vector<AdBlockCatalogEntry> catalog = GetAdBlockFilterCatalog();
  auto ids = [&catalog](const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    for (const AdBlockCatalogEntry& e :
         AdBlockCatalogueSubscriber::SelectSubscribedEntries(
             catalog, NormalizeCatalogLanguages(raw))) {
      out.push_back(e.id);
    }
    return out;
  };
  auto has = [](const std::vector<std::string>& v, std::string_view id) {
    return std::ranges::find(v, id) != v.end();
  };

  const std::vector<std::string> english = ids({"en-US", "en"});
  EXPECT_FALSE(has(english, "easylist-germany"));
  EXPECT_TRUE(has(english, "easylist"));

  const std::vector<std::string> german = ids({"de-AT"});
  EXPECT_TRUE(has(german, "easylist-germany"))
      << "the region subtag must not defeat the language match";
  EXPECT_TRUE(has(german, "easylist"))
      << "regional lists add to the global set, never replace it";

  const std::vector<std::string> multilingual = ids({"pt-BR", "uk", "zh-TW"});
  EXPECT_TRUE(has(multilingual, "easylist-portuguese"));
  EXPECT_TRUE(has(multilingual, "ruadlist"));
  EXPECT_TRUE(has(multilingual, "easylist-china"));
  EXPECT_FALSE(has(multilingual, "easylist-germany"));
}

// Garbage in the language prefs must select nothing extra.
TEST_F(AdBlockCatalogueSubscriberTest, MalformedLanguageTagsSelectNothing) {
  const std::vector<std::string> normalized = NormalizeCatalogLanguages(
      {"", "-", "x", "toolong", "DE-de", "de", "de", "12"});
  EXPECT_EQ((std::vector<std::string>{"de"}), normalized);
}

TEST_F(AdBlockCatalogueSubscriberTest, ConcatenatesEveryListWithASeparator) {
  std::string installed;
  int installs = 0;
  AdBlockCatalogueSubscriber subscriber(
      base::BindRepeating(
          [](const AdBlockCatalogEntry& entry,
             AdBlockCatalogueSubscriber::FetchCallback done) {
            // No trailing newline, which is the case that would splice two
            // rules together if the coordinator did not add a separator.
            std::move(done).Run(true, "||" + entry.id + ".example^",
                                std::string());
          }),
      base::BindRepeating(
          [](std::string* out, int* count, std::string rules,
             base::OnceClosure done) {
            *out = std::move(rules);
            ++*count;
            std::move(done).Run();
          },
          &installed, &installs),
      /*profile_languages=*/{});

  subscriber.Start();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, installs);
  EXPECT_EQ(1, subscriber.completed_rounds_for_testing());
  EXPECT_NE(installed.find("||easylist.example^\n"), std::string::npos)
      << "each list must be newline-terminated so rules cannot splice";
  EXPECT_NE(installed.find("||easyprivacy.example^\n"), std::string::npos);
}

// A round that loses one list must install nothing. Installing the rest would
// quietly narrow protection with no way for anyone to notice.
// One unreachable list must not cost the user every other list.
//
// This test previously asserted the opposite, on the reasoning that installing
// part of a set narrows protection without saying so. That reasoning inverts
// once the catalogue is Brave-sized: abandoning the round installs NOTHING and
// leaves the profile on the bundled baseline, which narrows protection far
// more than missing one list. The objection is answered instead by naming
// every failed list in the status, so the narrowing is reported.
TEST_F(AdBlockCatalogueSubscriberTest, OneFailedListDoesNotDiscardTheOthers) {
  int installs = 0;
  std::string installed;
  AdBlockCatalogueSubscriber subscriber(
      base::BindRepeating(
          [](const AdBlockCatalogEntry& entry,
             AdBlockCatalogueSubscriber::FetchCallback done) {
            if (entry.id == "easyprivacy") {
              std::move(done).Run(false, std::string(), "network error");
              return;
            }
            std::move(done).Run(true, "||ok.example^\n", std::string());
          }),
      base::BindRepeating(
          [](int* count, std::string* out, std::string rules,
             base::OnceClosure done) {
            ++*count;
            *out = std::move(rules);
            std::move(done).Run();
          },
          &installs, &installed),
      /*profile_languages=*/{});

  subscriber.Start();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, installs) << "the lists that did arrive must still be installed";
  EXPECT_EQ(1, subscriber.completed_rounds_for_testing());
  EXPECT_FALSE(installed.empty());
  EXPECT_NE(subscriber.last_error_for_testing().find("easyprivacy"),
            std::string::npos)
      << "a partial install must name the list that failed";
}

// The one case where installing nothing is right: if every list failed there is
// nothing to install, and replacing a working engine with an empty ruleset
// would be a total, silent loss of protection.
TEST_F(AdBlockCatalogueSubscriberTest, ARoundThatFetchedNothingInstallsNothing) {
  int installs = 0;
  AdBlockCatalogueSubscriber subscriber(
      base::BindRepeating(
          [](const AdBlockCatalogEntry& entry,
             AdBlockCatalogueSubscriber::FetchCallback done) {
            std::move(done).Run(false, std::string(), "network error");
          }),
      base::BindRepeating(
          [](int* count, std::string rules, base::OnceClosure done) {
            ++*count;
            std::move(done).Run();
          },
          &installs),
      /*profile_languages=*/{});

  subscriber.Start();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0, installs) << "a partial round must not be installed";
  EXPECT_EQ(0, subscriber.completed_rounds_for_testing());
  EXPECT_NE(subscriber.last_error_for_testing().find("easyprivacy"),
            std::string::npos)
      << "the failure must name the list that failed";
}

// And it must recover on the next interval rather than giving up for the
// lifetime of the profile.
TEST_F(AdBlockCatalogueSubscriberTest, RetriesOnTheNextIntervalAfterFailure) {
  int attempts = 0;
  int installs = 0;
  AdBlockCatalogueSubscriber subscriber(
      base::BindRepeating(
          [](int* attempts, const AdBlockCatalogEntry& entry,
             AdBlockCatalogueSubscriber::FetchCallback done) {
            ++*attempts;
            // Fail every list on the first round only.
            const bool fail = *attempts <= 1;
            std::move(done).Run(!fail, fail ? std::string() : "||ok.example^\n",
                                fail ? "transient" : std::string());
          },
          &attempts),
      base::BindRepeating(
          [](int* count, std::string rules, base::OnceClosure done) {
            ++*count;
            std::move(done).Run();
          },
          &installs),
      /*profile_languages=*/{});

  subscriber.Start();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, installs);

  task_environment_.FastForwardBy(base::Hours(25));
  EXPECT_EQ(1, installs) << "a later round must recover";
}

TEST_F(AdBlockCatalogueSubscriberTest, RefreshIntervalIsBoundedOnBothSides) {
  // The shortest declared interval wins, so every list meets its own freshness.
  const std::vector<AdBlockCatalogEntry> mixed = {
      MakeEntry("a", "https://example.test/a.txt", true,
                AdBlockListDelivery::kRuntimeDownload, 24),
      MakeEntry("b", "https://example.test/b.txt", true,
                AdBlockListDelivery::kRuntimeDownload, 6),
  };
  EXPECT_EQ(base::Hours(6),
            AdBlockCatalogueSubscriber::RefreshIntervalFor(mixed));

  // A hostile or mistaken interval cannot turn this into a request loop.
  const std::vector<AdBlockCatalogEntry> aggressive = {
      MakeEntry("a", "https://example.test/a.txt", true,
                AdBlockListDelivery::kRuntimeDownload, 0),
  };
  EXPECT_GE(AdBlockCatalogueSubscriber::RefreshIntervalFor(aggressive),
            base::Hours(1));
}

}  // namespace
}  // namespace seoul::adblock
