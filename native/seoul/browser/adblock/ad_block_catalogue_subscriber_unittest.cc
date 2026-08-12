// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/adblock/ad_block_catalogue_subscriber.h"

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
          GetAdBlockFilterCatalog());
  ASSERT_FALSE(selected.empty())
      << "the shipped catalog must subscribe to at least one upstream list";
  for (const AdBlockCatalogEntry& entry : selected) {
    EXPECT_TRUE(entry.enabled_by_default);
    EXPECT_EQ(AdBlockListDelivery::kRuntimeDownload, entry.delivery);
    EXPECT_EQ(0u, entry.url.rfind("https://", 0))
        << entry.id << " must be fetched over HTTPS";
  }
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
      AdBlockCatalogueSubscriber::SelectSubscribedEntries(catalog);
  ASSERT_EQ(1u, selected.size());
  EXPECT_EQ("good", selected.front().id);
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
          &installed, &installs));

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
TEST_F(AdBlockCatalogueSubscriberTest, OneFailedListAbandonsTheWholeRound) {
  int installs = 0;
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
          [](int* count, std::string rules, base::OnceClosure done) {
            ++*count;
            std::move(done).Run();
          },
          &installs));

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
          &installs));

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
