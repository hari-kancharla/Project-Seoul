// Project Seoul native blocker request, host, service, and stats tests.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "base/version.h"
#include "crypto/sha2.h"
#include "seoul/browser/adblock/ad_block_engine_host.h"
#include "seoul/browser/adblock/ad_block_filter_list_manager.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_stats_service.h"
#include "services/network/public/cpp/resource_request.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul::adblock {
namespace {

std::vector<uint8_t> RuleBytes(std::string_view rules) {
  return std::vector<uint8_t>(rules.begin(), rules.end());
}

std::string RuleDigest(std::string_view rules) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(rules)));
}

void WriteFilterPackage(const base::FilePath& path,
                        std::string_view version,
                        std::string_view default_rules,
                        std::string_view additional_rules) {
  ASSERT_TRUE(base::CreateDirectory(path));
  ASSERT_TRUE(base::WriteFile(path.AppendASCII("default.txt"), default_rules));
  ASSERT_TRUE(
      base::WriteFile(path.AppendASCII("additional.txt"), additional_rules));

  base::DictValue manifest;
  manifest.Set("format", "seoul-adblock-filter-set");
  manifest.Set("schema_version", 1);
  manifest.Set("version", version);
  manifest.Set("default_sha256", RuleDigest(default_rules));
  manifest.Set("additional_sha256", RuleDigest(additional_rules));
  std::optional<std::string> manifest_json = base::WriteJson(manifest);
  ASSERT_TRUE(manifest_json);
  ASSERT_TRUE(
      base::WriteFile(path.AppendASCII("manifest.json"), *manifest_json));
}

AdBlockRequest ScriptRequest(std::string url) {
  const GURL parsed(url);
  return AdBlockRequest(std::move(url), std::string(parsed.host()),
                        "news.example", "script", true);
}

AdBlockRequest ModeRequest(std::string url,
                           bool is_third_party,
                           AdBlockMode mode) {
  const GURL parsed(url);
  AdBlockRequest request(std::move(url), std::string(parsed.host()),
                         "news.example", "script", is_third_party);
  request.mode = mode;
  return request;
}

TEST(AdBlockRequestTest, MapsEveryRequiredResourceClass) {
  EXPECT_EQ("main_frame",
            AdBlockRequestTypeFor(
                static_cast<int>(blink::mojom::ResourceType::kMainFrame),
                network::mojom::RequestDestination::kDocument));
  EXPECT_EQ("sub_frame",
            AdBlockRequestTypeFor(
                static_cast<int>(blink::mojom::ResourceType::kSubFrame),
                network::mojom::RequestDestination::kIframe));
  EXPECT_EQ("script", AdBlockRequestTypeFor(
                          static_cast<int>(blink::mojom::ResourceType::kScript),
                          network::mojom::RequestDestination::kScript));
  EXPECT_EQ("stylesheet",
            AdBlockRequestTypeFor(
                static_cast<int>(blink::mojom::ResourceType::kStylesheet),
                network::mojom::RequestDestination::kStyle));
  EXPECT_EQ("image", AdBlockRequestTypeFor(
                         static_cast<int>(blink::mojom::ResourceType::kImage),
                         network::mojom::RequestDestination::kImage));
  EXPECT_EQ("font",
            AdBlockRequestTypeFor(
                static_cast<int>(blink::mojom::ResourceType::kFontResource),
                network::mojom::RequestDestination::kFont));
  EXPECT_EQ("media", AdBlockRequestTypeFor(
                         static_cast<int>(blink::mojom::ResourceType::kMedia),
                         network::mojom::RequestDestination::kVideo));
  EXPECT_EQ(
      "xmlhttprequest",
      AdBlockRequestTypeFor(static_cast<int>(blink::mojom::ResourceType::kXhr),
                            network::mojom::RequestDestination::kEmpty));
  EXPECT_EQ("ping", AdBlockRequestTypeFor(
                        static_cast<int>(blink::mojom::ResourceType::kPing),
                        network::mojom::RequestDestination::kEmpty));
  EXPECT_EQ("object", AdBlockRequestTypeFor(
                          static_cast<int>(blink::mojom::ResourceType::kObject),
                          network::mojom::RequestDestination::kObject));
  EXPECT_EQ("other",
            AdBlockRequestTypeFor(
                static_cast<int>(blink::mojom::ResourceType::kSubResource),
                network::mojom::RequestDestination::kManifest));
}

TEST(AdBlockRequestTest, UsesPrivateRegistriesForPartyCalculation) {
  const GURL request_url("https://a.blogspot.com/tracker.js");
  const url::Origin top_frame =
      url::Origin::Create(GURL("https://b.blogspot.com/article"));
  EXPECT_TRUE(IsThirdPartyRequest(request_url, top_frame, std::nullopt));

  const url::Origin same_site =
      url::Origin::Create(GURL("https://news.example.com/article"));
  EXPECT_FALSE(IsThirdPartyRequest(GURL("https://cdn.example.com/app.js"),
                                   same_site, std::nullopt));
  EXPECT_FALSE(IsThirdPartyRequest(request_url, std::nullopt, std::nullopt));
}

TEST(AdBlockRequestTest, SupportsOnlyNetworkSchemes) {
  EXPECT_TRUE(IsSupportedRequestScheme(GURL("https://example.com/")));
  EXPECT_TRUE(IsSupportedRequestScheme(GURL("http://example.com/")));
  EXPECT_TRUE(IsSupportedRequestScheme(GURL("wss://example.com/socket")));
  EXPECT_TRUE(IsSupportedRequestScheme(GURL("ws://example.com/socket")));
  EXPECT_FALSE(IsSupportedRequestScheme(GURL("chrome://settings/")));
  EXPECT_FALSE(IsSupportedRequestScheme(GURL("file:///tmp/file")));
  EXPECT_FALSE(IsSupportedRequestScheme(GURL("data:text/plain,hello")));
}

TEST(AdBlockRequestTest, UrlRewriteCanOnlyRemoveExistingQueryPairs) {
  const GURL original(
      "https://user:pass@news.example:443/article?id=7&utm=a&utm=b#part");
  EXPECT_TRUE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/article?id=7&utm=b#part"),
      "GET"));
  EXPECT_TRUE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/article#part"), "HEAD"));
  EXPECT_FALSE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/article?id=8#part"),
      "GET"));
  EXPECT_FALSE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://other.example/article?id=7#part"), "GET"));
  EXPECT_FALSE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/other?id=7#part"), "GET"));
  EXPECT_FALSE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/article?id=7#other"),
      "GET"));
  EXPECT_FALSE(IsSafeAdBlockUrlRewrite(
      original, GURL("https://user:pass@news.example/article?id=7#part"),
      "POST"));
}

TEST(AdBlockRequestTest, BuildsCompleteTrustedRequestMetadata) {
  network::ResourceRequest request;
  request.url = GURL("https://cdn.example.net/app.js");
  request.method = "POST";
  request.resource_type = static_cast<int>(blink::mojom::ResourceType::kScript);
  request.destination = network::mojom::RequestDestination::kScript;
  request.request_initiator =
      url::Origin::Create(GURL("https://frame.example.com/"));
  request.devtools_request_id = "devtools-id";
  request.originated_from_service_worker = true;
  const url::Origin top_frame =
      url::Origin::Create(GURL("https://news.example.com/"));

  const AdBlockRequest result =
      BuildAdBlockRequest(request, top_frame, std::nullopt,
                          AdBlockFactoryType::kServiceWorkerSubresource);
  EXPECT_EQ(request.url.spec(), result.url);
  EXPECT_EQ("cdn.example.net", result.hostname);
  EXPECT_EQ("news.example.com", result.source_hostname);
  EXPECT_EQ("script", result.request_type);
  EXPECT_EQ("POST", result.method);
  EXPECT_TRUE(result.is_third_party);
  EXPECT_EQ("https://frame.example.com/", result.initiator_url);
  EXPECT_EQ("https://news.example.com/", result.outermost_top_frame_url);
  EXPECT_EQ("devtools-id", result.devtools_request_id);
  EXPECT_TRUE(result.originated_from_service_worker);
  EXPECT_EQ(AdBlockFactoryType::kServiceWorkerSubresource, result.factory_type);
}

TEST(AdBlockRequestTest, BuildsTrustedWebSocketMetadata) {
  const url::Origin top_frame =
      url::Origin::Create(GURL("https://news.example.com/"));
  const url::Origin initiator =
      url::Origin::Create(GURL("https://frame.example.com/"));
  const AdBlockRequest result =
      BuildWebSocketAdBlockRequest(GURL("wss://socket.example.net/live"),
                                   top_frame, initiator, std::nullopt);

  EXPECT_EQ("wss://socket.example.net/live", result.url);
  EXPECT_EQ("socket.example.net", result.hostname);
  EXPECT_EQ("news.example.com", result.source_hostname);
  EXPECT_EQ("websocket", result.request_type);
  EXPECT_EQ("GET", result.method);
  EXPECT_TRUE(result.is_third_party);
  EXPECT_EQ("https://frame.example.com/", result.initiator_url);
  EXPECT_EQ("https://news.example.com/", result.outermost_top_frame_url);
  EXPECT_EQ(AdBlockFactoryType::kWebSocket, result.factory_type);
}

TEST(AdBlockRequestTest, BuildsTopLevelNavigationMetadata) {
  const url::Origin initiator =
      url::Origin::Create(GURL("https://source.example/"));
  const AdBlockRequest result = BuildNavigationAdBlockRequest(
      GURL("https://destination.example/path"), initiator, "POST");

  EXPECT_EQ("https://destination.example/path", result.url);
  EXPECT_EQ("destination.example", result.hostname);
  EXPECT_EQ("destination.example", result.source_hostname);
  EXPECT_EQ("main_frame", result.request_type);
  EXPECT_EQ("POST", result.method);
  EXPECT_FALSE(result.is_third_party);
  EXPECT_EQ("https://source.example/", result.initiator_url);
  EXPECT_EQ("https://destination.example/path", result.outermost_top_frame_url);
  EXPECT_EQ(AdBlockFactoryType::kNavigation, result.factory_type);
}

class AdBlockAsyncTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(AdBlockAsyncTest, EngineReplacementIsAtomicAndRetainsLastGoodEngine) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
  host.ReplaceRules(RuleBytes("||ads.example^\n"),
                    replace_future.GetCallback());
  ASSERT_TRUE(replace_future.Get().success);

  base::test::TestFuture<AdBlockEngineEvaluationResult> first_match;
  host.Evaluate(ScriptRequest("https://ads.example/banner.js"),
                first_match.GetCallback());
  EXPECT_TRUE(first_match.Get().match.matched);

  base::test::TestFuture<AdBlockEngineReplaceResult> failed_replace;
  host.ReplaceRules(std::vector<uint8_t>{0xff, 0xfe},
                    failed_replace.GetCallback());
  EXPECT_FALSE(failed_replace.Get().success);

  base::test::TestFuture<AdBlockEngineEvaluationResult> retained_match;
  host.Evaluate(ScriptRequest("https://ads.example/banner.js"),
                retained_match.GetCallback());
  EXPECT_TRUE(retained_match.Get().match.matched);
}

TEST_F(AdBlockAsyncTest,
       TwoEngineReplacementIsAtomicWhenAdditionalRulesAreMalformed) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> initial_replace;
  host.ReplaceRuleSets(RuleBytes("||default-old.example^\n"),
                       RuleBytes("||additional-old.example^\n"),
                       initial_replace.GetCallback());
  ASSERT_TRUE(initial_replace.Get().success);

  base::test::TestFuture<AdBlockEngineReplaceResult> failed_replace;
  host.ReplaceRuleSets(RuleBytes("||default-new.example^\n"),
                       std::vector<uint8_t>{0xff, 0xfe},
                       failed_replace.GetCallback());
  EXPECT_FALSE(failed_replace.Get().success);

  base::test::TestFuture<AdBlockEngineEvaluationResult> retained_default;
  host.Evaluate(ScriptRequest("https://default-old.example/ad.js"),
                retained_default.GetCallback());
  EXPECT_TRUE(retained_default.Get().match.matched);

  base::test::TestFuture<AdBlockEngineEvaluationResult> rejected_default;
  host.Evaluate(ScriptRequest("https://default-new.example/ad.js"),
                rejected_default.GetCallback());
  EXPECT_FALSE(rejected_default.Get().match.matched);

  base::test::TestFuture<AdBlockEngineEvaluationResult> retained_additional;
  host.Evaluate(ScriptRequest("https://additional-old.example/ad.js"),
                retained_additional.GetCallback());
  EXPECT_TRUE(retained_additional.Get().match.matched);
}

TEST_F(AdBlockAsyncTest, FilterListStartupUsesBundledBaselineWithoutCache) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  AdBlockEngineHost host;
  AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);

  base::test::TestFuture<AdBlockFilterListUpdateStatus> started;
  manager.Start(started.GetCallback());
  const AdBlockFilterListUpdateStatus status = started.Get();
  EXPECT_EQ(AdBlockFilterListState::kReady, status.state);
  EXPECT_EQ(AdBlockFilterListSource::kBundled, status.source);
  EXPECT_EQ("0.1.0", status.version);
  EXPECT_TRUE(status.last_error.empty());
}

TEST_F(AdBlockAsyncTest,
       VerifiedFilterComponentActivatesAndRestartsFromLastKnownGoodCache) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  const base::FilePath component_path =
      profile_dir.GetPath().AppendASCII("verified-component");
  WriteFilterPackage(component_path, "1.2.3", "||verified-default.example^\n",
                     "||verified-additional.example^\n");

  {
    AdBlockEngineHost host;
    AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);
    base::test::TestFuture<AdBlockFilterListUpdateStatus> activated;
    manager.ActivateVerifiedComponent(component_path, base::Version("1.2.3"),
                                      activated.GetCallback());
    const AdBlockFilterListUpdateStatus status = activated.Get();
    EXPECT_EQ(AdBlockFilterListState::kReady, status.state);
    EXPECT_EQ(AdBlockFilterListSource::kVerifiedComponent, status.source);
    EXPECT_EQ("1.2.3", status.version);
    EXPECT_TRUE(status.last_error.empty());

    base::test::TestFuture<AdBlockEngineEvaluationResult> live_match;
    host.Evaluate(ScriptRequest("https://verified-default.example/ad.js"),
                  live_match.GetCallback());
    EXPECT_TRUE(live_match.Get().match.matched);
  }

  AdBlockEngineHost restarted_host;
  AdBlockFilterListManager restarted_manager(&restarted_host,
                                             profile_dir.GetPath(), nullptr);
  base::test::TestFuture<AdBlockFilterListUpdateStatus> restarted;
  restarted_manager.Start(restarted.GetCallback());
  const AdBlockFilterListUpdateStatus restarted_status = restarted.Get();
  EXPECT_EQ(AdBlockFilterListState::kReady, restarted_status.state);
  EXPECT_EQ(AdBlockFilterListSource::kCache, restarted_status.source);
  EXPECT_EQ("1.2.3", restarted_status.version);

  base::test::TestFuture<AdBlockEngineEvaluationResult> cached_match;
  restarted_host.Evaluate(
      ScriptRequest("https://verified-additional.example/ad.js"),
      cached_match.GetCallback());
  EXPECT_TRUE(cached_match.Get().match.matched);
}

TEST_F(AdBlockAsyncTest,
       DamagedActiveCacheSlotFallsBackToPreviousKnownGoodSlot) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  const base::FilePath first_component =
      profile_dir.GetPath().AppendASCII("slot-first-component");
  const base::FilePath second_component =
      profile_dir.GetPath().AppendASCII("slot-second-component");
  WriteFilterPackage(first_component, "4.0.0", "||slot-first.example^\n",
                     std::string_view());
  WriteFilterPackage(second_component, "4.1.0", "||slot-second.example^\n",
                     std::string_view());

  {
    AdBlockEngineHost host;
    AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);
    base::test::TestFuture<AdBlockFilterListUpdateStatus> first_activation;
    manager.ActivateVerifiedComponent(first_component, base::Version("4.0.0"),
                                      first_activation.GetCallback());
    ASSERT_EQ(AdBlockFilterListState::kReady, first_activation.Get().state);

    base::test::TestFuture<AdBlockFilterListUpdateStatus> second_activation;
    manager.ActivateVerifiedComponent(second_component, base::Version("4.1.0"),
                                      second_activation.GetCallback());
    ASSERT_EQ(AdBlockFilterListState::kReady, second_activation.Get().state);
  }

  const base::FilePath cache_path =
      profile_dir.GetPath().AppendASCII("SeoulAdBlock");
  std::string active_slot;
  ASSERT_TRUE(base::ReadFileToString(cache_path.AppendASCII("active-slot"),
                                     &active_slot));
  base::TrimWhitespaceASCII(active_slot, base::TRIM_ALL, &active_slot);
  ASSERT_TRUE(active_slot == "a" || active_slot == "b");
  ASSERT_TRUE(base::WriteFile(
      cache_path.AppendASCII(active_slot).AppendASCII("manifest.json"),
      "{not-json"));

  AdBlockEngineHost restarted_host;
  AdBlockFilterListManager restarted_manager(&restarted_host,
                                             profile_dir.GetPath(), nullptr);
  base::test::TestFuture<AdBlockFilterListUpdateStatus> restarted;
  restarted_manager.Start(restarted.GetCallback());
  const AdBlockFilterListUpdateStatus status = restarted.Get();
  EXPECT_EQ(AdBlockFilterListState::kReady, status.state);
  EXPECT_EQ(AdBlockFilterListSource::kCache, status.source);
  EXPECT_EQ("4.0.0", status.version);
  EXPECT_NE(std::string::npos,
            status.last_error.find("active cache slot invalid"));

  base::test::TestFuture<AdBlockEngineEvaluationResult> fallback_match;
  restarted_host.Evaluate(ScriptRequest("https://slot-first.example/ad.js"),
                          fallback_match.GetCallback());
  EXPECT_TRUE(fallback_match.Get().match.matched);

  base::test::TestFuture<AdBlockEngineEvaluationResult> damaged_slot_match;
  restarted_host.Evaluate(ScriptRequest("https://slot-second.example/ad.js"),
                          damaged_slot_match.GetCallback());
  EXPECT_FALSE(damaged_slot_match.Get().match.matched);
}

TEST_F(AdBlockAsyncTest,
       PinnedAdditionalRuleSetPersistsAndDownloadFailureRetainsIt) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());

  {
    AdBlockEngineHost host;
    AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);
    base::test::TestFuture<AdBlockFilterListUpdateStatus> started;
    manager.Start(started.GetCallback());
    ASSERT_EQ(AdBlockFilterListState::kReady, started.Get().state);

    base::test::TestFuture<AdBlockFilterListUpdateStatus> activated;
    manager.ActivatePinnedAdditionalRuleSet("||pinned-subscription.example^\n",
                                            base::Version("5.0.0"),
                                            activated.GetCallback());
    const AdBlockFilterListUpdateStatus status = activated.Get();
    EXPECT_EQ(AdBlockFilterListState::kReady, status.state);
    EXPECT_EQ(AdBlockFilterListSource::kPinnedSubscription, status.source);
    EXPECT_EQ("5.0.0", status.version);

    base::test::TestFuture<AdBlockEngineEvaluationResult> live_match;
    host.Evaluate(ScriptRequest("https://pinned-subscription.example/ad.js"),
                  live_match.GetCallback());
    EXPECT_TRUE(live_match.Get().match.matched);

    base::test::TestFuture<AdBlockFilterListUpdateStatus> failed_update;
    manager.ReportUpdateFailure("simulated HTTPS failure",
                                failed_update.GetCallback());
    const AdBlockFilterListUpdateStatus failed = failed_update.Get();
    EXPECT_EQ(AdBlockFilterListState::kError, failed.state);
    EXPECT_EQ(AdBlockFilterListSource::kPinnedSubscription, failed.source);
    EXPECT_EQ("5.0.0", failed.version);

    base::test::TestFuture<AdBlockEngineEvaluationResult> retained_match;
    host.Evaluate(ScriptRequest("https://pinned-subscription.example/ad.js"),
                  retained_match.GetCallback());
    EXPECT_TRUE(retained_match.Get().match.matched);
  }

  AdBlockEngineHost restarted_host;
  AdBlockFilterListManager restarted_manager(&restarted_host,
                                             profile_dir.GetPath(), nullptr);
  base::test::TestFuture<AdBlockFilterListUpdateStatus> restarted;
  restarted_manager.Start(restarted.GetCallback());
  const AdBlockFilterListUpdateStatus restarted_status = restarted.Get();
  EXPECT_EQ(AdBlockFilterListState::kReady, restarted_status.state);
  EXPECT_EQ(AdBlockFilterListSource::kCache, restarted_status.source);
  EXPECT_EQ("5.0.0", restarted_status.version);

  base::test::TestFuture<AdBlockEngineEvaluationResult> cached_match;
  restarted_host.Evaluate(
      ScriptRequest("https://pinned-subscription.example/ad.js"),
      cached_match.GetCallback());
  EXPECT_TRUE(cached_match.Get().match.matched);
}

TEST_F(AdBlockAsyncTest,
       HashMismatchRejectsUpdateAndRetainsPreviouslyActiveRules) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  const base::FilePath first_component =
      profile_dir.GetPath().AppendASCII("first-component");
  WriteFilterPackage(first_component, "2.0.0", "||last-good.example^\n",
                     std::string_view());

  AdBlockEngineHost host;
  AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);
  base::test::TestFuture<AdBlockFilterListUpdateStatus> first_activation;
  manager.ActivateVerifiedComponent(first_component, base::Version("2.0.0"),
                                    first_activation.GetCallback());
  ASSERT_EQ(AdBlockFilterListState::kReady, first_activation.Get().state);

  const base::FilePath corrupt_component =
      profile_dir.GetPath().AppendASCII("corrupt-component");
  WriteFilterPackage(corrupt_component, "2.1.0",
                     "||should-not-activate.example^\n", std::string_view());
  ASSERT_TRUE(base::WriteFile(corrupt_component.AppendASCII("default.txt"),
                              "||tampered.example^\n"));

  base::test::TestFuture<AdBlockFilterListUpdateStatus> failed_activation;
  manager.ActivateVerifiedComponent(corrupt_component, base::Version("2.1.0"),
                                    failed_activation.GetCallback());
  const AdBlockFilterListUpdateStatus failed = failed_activation.Get();
  EXPECT_EQ(AdBlockFilterListState::kError, failed.state);
  EXPECT_NE(std::string::npos, failed.last_error.find("hash mismatch"));

  base::test::TestFuture<AdBlockEngineEvaluationResult> retained_match;
  host.Evaluate(ScriptRequest("https://last-good.example/ad.js"),
                retained_match.GetCallback());
  EXPECT_TRUE(retained_match.Get().match.matched);

  base::test::TestFuture<AdBlockEngineEvaluationResult> tampered_match;
  host.Evaluate(ScriptRequest("https://tampered.example/ad.js"),
                tampered_match.GetCallback());
  EXPECT_FALSE(tampered_match.Get().match.matched);
}

TEST_F(AdBlockAsyncTest,
       TextValidationFailureRejectsBothListsAndRetainsPreviousPackage) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  const base::FilePath first_component =
      profile_dir.GetPath().AppendASCII("parse-good-component");
  WriteFilterPackage(first_component, "3.0.0", "||parse-last-good.example^\n",
                     std::string_view());

  AdBlockEngineHost host;
  AdBlockFilterListManager manager(&host, profile_dir.GetPath(), nullptr);
  base::test::TestFuture<AdBlockFilterListUpdateStatus> first_activation;
  manager.ActivateVerifiedComponent(first_component, base::Version("3.0.0"),
                                    first_activation.GetCallback());
  ASSERT_EQ(AdBlockFilterListState::kReady, first_activation.Get().state);

  const base::FilePath malformed_component =
      profile_dir.GetPath().AppendASCII("parse-bad-component");
  const std::string invalid_utf8("\xff\xfe", 2);
  WriteFilterPackage(malformed_component, "3.1.0",
                     "||parse-new-default.example^\n", invalid_utf8);

  base::test::TestFuture<AdBlockFilterListUpdateStatus> failed_activation;
  manager.ActivateVerifiedComponent(malformed_component, base::Version("3.1.0"),
                                    failed_activation.GetCallback());
  const AdBlockFilterListUpdateStatus failed = failed_activation.Get();
  EXPECT_EQ(AdBlockFilterListState::kError, failed.state);

  base::test::TestFuture<AdBlockEngineEvaluationResult> retained_match;
  host.Evaluate(ScriptRequest("https://parse-last-good.example/ad.js"),
                retained_match.GetCallback());
  EXPECT_TRUE(retained_match.Get().match.matched);

  base::test::TestFuture<AdBlockEngineEvaluationResult> rejected_match;
  host.Evaluate(ScriptRequest("https://parse-new-default.example/ad.js"),
                rejected_match.GetCallback());
  EXPECT_FALSE(rejected_match.Get().match.matched);
}

TEST(AdBlockEngineWorkerTest, AppliesDefaultRulesByModeAndParty) {
  AdBlockEngineWorker worker;
  ASSERT_TRUE(worker
                  .ReplaceRules(AdBlockEngineGroup::kDefault,
                                RuleBytes("||ads.example^\n"
                                          "||news.example/ad.js\n"))
                  .success);

  AdBlockEngineEvaluationResult third_party = worker.Evaluate(ModeRequest(
      "https://ads.example/banner.js", true, AdBlockMode::kStandard));
  EXPECT_TRUE(third_party.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kDefault, third_party.deciding_engine);

  AdBlockEngineEvaluationResult first_party_standard = worker.Evaluate(
      ModeRequest("https://news.example/ad.js", false, AdBlockMode::kStandard));
  EXPECT_FALSE(first_party_standard.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kNone, first_party_standard.deciding_engine);

  AdBlockEngineEvaluationResult first_party_aggressive =
      worker.Evaluate(ModeRequest("https://news.example/ad.js", false,
                                  AdBlockMode::kAggressive));
  EXPECT_TRUE(first_party_aggressive.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kDefault,
            first_party_aggressive.deciding_engine);

  AdBlockEngineEvaluationResult disabled = worker.Evaluate(
      ModeRequest("https://ads.example/banner.js", true, AdBlockMode::kOff));
  EXPECT_FALSE(disabled.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kNone, disabled.deciding_engine);
}

TEST(AdBlockEngineWorkerTest,
     AdditionalRulesUseAggressiveSemanticsAndMergeExceptions) {
  AdBlockEngineWorker worker;
  ASSERT_TRUE(worker
                  .ReplaceRules(AdBlockEngineGroup::kDefault,
                                RuleBytes("||ads.example^\n"
                                          "@@||allowed.example^\n"))
                  .success);
  ASSERT_TRUE(worker
                  .ReplaceRules(AdBlockEngineGroup::kAdditional,
                                RuleBytes("||news.example/custom.js\n"
                                          "@@||ads.example/allowed.js\n"
                                          "||allowed.example^\n"))
                  .success);

  AdBlockEngineEvaluationResult custom_first_party =
      worker.Evaluate(ModeRequest("https://news.example/custom.js", false,
                                  AdBlockMode::kStandard));
  EXPECT_TRUE(custom_first_party.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kAdditional,
            custom_first_party.deciding_engine);

  AdBlockEngineEvaluationResult additional_exception =
      worker.Evaluate(ModeRequest("https://ads.example/allowed.js", true,
                                  AdBlockMode::kStandard));
  EXPECT_TRUE(additional_exception.match.matched);
  EXPECT_TRUE(additional_exception.match.has_exception);
  EXPECT_EQ(AdBlockDecidingEngine::kAdditional,
            additional_exception.deciding_engine);

  AdBlockEngineEvaluationResult default_exception = worker.Evaluate(ModeRequest(
      "https://allowed.example/resource.js", true, AdBlockMode::kStandard));
  // The subset evaluator preserves that a prior engine produced a decision
  // while carrying the default exception forward. Blocking still requires a
  // match without an exception.
  EXPECT_TRUE(default_exception.match.matched);
  EXPECT_TRUE(default_exception.match.has_exception);
  EXPECT_EQ(AdBlockDecidingEngine::kDefault, default_exception.deciding_engine);
}

TEST(AdBlockEngineWorkerTest, DefaultImportantRuleAppliesFirstPartyInStandard) {
  AdBlockEngineWorker worker;
  ASSERT_TRUE(worker
                  .ReplaceRules(AdBlockEngineGroup::kDefault,
                                RuleBytes("||news.example/forced.js$important\n"
                                          "@@||news.example/forced.js\n"))
                  .success);

  AdBlockEngineEvaluationResult result = worker.Evaluate(ModeRequest(
      "https://news.example/forced.js", false, AdBlockMode::kStandard));
  EXPECT_TRUE(result.match.matched);
  EXPECT_TRUE(result.match.important);
  EXPECT_FALSE(result.match.has_exception);
  EXPECT_EQ(AdBlockDecidingEngine::kDefault, result.deciding_engine);
}

TEST(AdBlockEngineWorkerTest, FailedAdditionalReplacementRetainsLastGoodRules) {
  AdBlockEngineWorker worker;
  ASSERT_TRUE(worker
                  .ReplaceRules(AdBlockEngineGroup::kAdditional,
                                RuleBytes("||custom.example^\n"))
                  .success);
  EXPECT_FALSE(worker
                   .ReplaceRules(AdBlockEngineGroup::kAdditional,
                                 std::vector<uint8_t>{0xff, 0xfe})
                   .success);

  AdBlockEngineEvaluationResult result = worker.Evaluate(ModeRequest(
      "https://custom.example/script.js", true, AdBlockMode::kStandard));
  EXPECT_TRUE(result.match.matched);
  EXPECT_EQ(AdBlockDecidingEngine::kAdditional, result.deciding_engine);
}

TEST(AdBlockEngineWorkerTest,
     AppliesOnlySafeAdditionalRewritesAndRechecksTarget) {
  AdBlockEngineWorker worker;
  ASSERT_TRUE(
      worker
          .ReplaceRules(AdBlockEngineGroup::kDefault,
                        RuleBytes("||news.example^$removeparam=default\n"))
          .success);
  ASSERT_TRUE(
      worker
          .ReplaceRules(
              AdBlockEngineGroup::kAdditional,
              RuleBytes("||news.example^$removeparam=utm\n"
                        "|https://news.example/article?id=7|$xmlhttprequest\n"))
          .success);

  AdBlockRequest safe =
      ModeRequest("https://news.example/article?id=8&utm=tracking", false,
                  AdBlockMode::kStandard);
  safe.request_type = "xmlhttprequest";
  const AdBlockEngineEvaluationResult rewritten = worker.Evaluate(safe);
  EXPECT_FALSE(rewritten.match.matched);
  ASSERT_TRUE(rewritten.match.rewritten_url);
  EXPECT_EQ("https://news.example/article?id=8",
            *rewritten.match.rewritten_url);
  EXPECT_EQ(AdBlockDecidingEngine::kAdditional, rewritten.deciding_engine);

  safe.method = "POST";
  EXPECT_FALSE(worker.Evaluate(safe).match.rewritten_url);

  AdBlockRequest blocked_target =
      ModeRequest("https://news.example/article?id=7&utm=tracking", false,
                  AdBlockMode::kStandard);
  blocked_target.request_type = "xmlhttprequest";
  const AdBlockEngineEvaluationResult target_blocked =
      worker.Evaluate(std::move(blocked_target));
  EXPECT_TRUE(target_blocked.match.matched);
  EXPECT_FALSE(target_blocked.match.has_exception);
  EXPECT_FALSE(target_blocked.match.rewritten_url);

  AdBlockRequest default_candidate =
      ModeRequest("https://news.example/article?default=1", false,
                  AdBlockMode::kAggressive);
  default_candidate.request_type = "xmlhttprequest";
  const AdBlockEngineEvaluationResult default_ignored =
      worker.Evaluate(std::move(default_candidate));
  EXPECT_FALSE(default_ignored.match.rewritten_url);
}

TEST_F(AdBlockAsyncTest, ServiceReturnsStructuredDecisionAndRecordsStats) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
  service.ReplaceRulesForTesting(RuleBytes("||ads.example^\n"),
                                 replace_future.GetCallback());
  ASSERT_TRUE(replace_future.Get().success);

  AdBlockRequest request = ScriptRequest("https://ads.example/banner.js");
  const content::GlobalRenderFrameHostToken frame_token;
  request.render_frame_token = frame_token;
  base::test::TestFuture<AdBlockDecision> decision_future;
  service.CheckRequest(std::move(request), decision_future.GetCallback());
  const AdBlockDecision& decision = decision_future.Get();
  EXPECT_EQ(AdBlockAction::kBlock, decision.action);
  EXPECT_EQ(AdBlockDecidingEngine::kDefault, decision.deciding_engine);
  EXPECT_EQ(AdBlockRuleCategory::kNetwork, decision.rule_category);
  EXPECT_TRUE(decision.matched);
  EXPECT_EQ(1u, service.stats()->total_blocked_count());
  EXPECT_EQ(1u, service.stats()->GetBlockedCount(frame_token));
}

TEST_F(AdBlockAsyncTest, ServiceReturnsVettedRedirectAndRewriteActions) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> default_future;
  service.ReplaceRulesForTesting(
      RuleBytes("||ads.example/noop.js$redirect=noopjs\n"),
      default_future.GetCallback());
  ASSERT_TRUE(default_future.Get().success);
  base::test::TestFuture<AdBlockEngineReplaceResult> additional_future;
  service.ReplaceAdditionalRulesForTesting(
      RuleBytes("||news.example^$removeparam=utm\n"),
      additional_future.GetCallback());
  ASSERT_TRUE(additional_future.Get().success);

  base::test::TestFuture<AdBlockDecision> redirect_future;
  service.CheckRequest(
      ModeRequest("https://ads.example/noop.js", true, AdBlockMode::kStandard),
      redirect_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kRedirect, redirect_future.Get().action);
  EXPECT_EQ(AdBlockRuleCategory::kRedirect,
            redirect_future.Get().rule_category);
  EXPECT_TRUE(redirect_future.Get().redirect);
  EXPECT_EQ(1u, service.stats()->total_blocked_count());

  base::test::TestFuture<AdBlockDecision> rewrite_future;
  AdBlockRequest rewrite_request =
      ModeRequest("https://news.example/article?utm=tracking", false,
                  AdBlockMode::kStandard);
  rewrite_request.request_type = "xmlhttprequest";
  service.CheckRequest(std::move(rewrite_request),
                       rewrite_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kRewrite, rewrite_future.Get().action);
  EXPECT_EQ(AdBlockRuleCategory::kRewrite, rewrite_future.Get().rule_category);
  ASSERT_TRUE(rewrite_future.Get().rewritten_url);
  EXPECT_EQ("https://news.example/article",
            *rewrite_future.Get().rewritten_url);
  EXPECT_EQ(1u, service.stats()->total_blocked_count());
}

TEST_F(AdBlockAsyncTest, RedirectRuleRequiresUnsuppressedBlockingMatch) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
  service.ReplaceRulesForTesting(RuleBytes("noop.js$redirect-rule=noopjs\n"
                                           "excepted.js$redirect=noopjs\n"
                                           "@@||ads.example/excepted.js\n"),
                                 replace_future.GetCallback());
  ASSERT_TRUE(replace_future.Get().success);

  base::test::TestFuture<AdBlockDecision> redirect_rule_future;
  service.CheckRequest(
      ModeRequest("https://ads.example/noop.js", true, AdBlockMode::kStandard),
      redirect_rule_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kAllow, redirect_rule_future.Get().action);
  EXPECT_EQ(AdBlockRuleCategory::kNone,
            redirect_rule_future.Get().rule_category);

  base::test::TestFuture<AdBlockDecision> exception_future;
  service.CheckRequest(ModeRequest("https://ads.example/excepted.js", true,
                                   AdBlockMode::kStandard),
                       exception_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kAllow, exception_future.Get().action);
  EXPECT_EQ(AdBlockRuleCategory::kException,
            exception_future.Get().rule_category);
  EXPECT_EQ(0u, service.stats()->total_blocked_count());
}

TEST_F(AdBlockAsyncTest, ServiceAllowsCombinedExceptionAndDoesNotCountBlock) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> default_future;
  service.ReplaceRulesForTesting(RuleBytes("||ads.example^\n"),
                                 default_future.GetCallback());
  ASSERT_TRUE(default_future.Get().success);
  base::test::TestFuture<AdBlockEngineReplaceResult> additional_future;
  service.ReplaceAdditionalRulesForTesting(
      RuleBytes("@@||ads.example/allowed.js\n"),
      additional_future.GetCallback());
  ASSERT_TRUE(additional_future.Get().success);

  base::test::TestFuture<AdBlockDecision> decision_future;
  service.CheckRequest(ModeRequest("https://ads.example/allowed.js", true,
                                   AdBlockMode::kStandard),
                       decision_future.GetCallback());
  const AdBlockDecision& decision = decision_future.Get();
  EXPECT_EQ(AdBlockAction::kAllow, decision.action);
  EXPECT_EQ(AdBlockRuleCategory::kException, decision.rule_category);
  EXPECT_TRUE(decision.matched);
  EXPECT_TRUE(decision.has_exception);
  EXPECT_EQ(AdBlockDecidingEngine::kAdditional, decision.deciding_engine);
  EXPECT_EQ(0u, service.stats()->total_blocked_count());
}

TEST_F(AdBlockAsyncTest, ServiceBypassesInternalSchemesBeforeEngineWork) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
  service.ReplaceRulesForTesting(RuleBytes("*$script\n"),
                                 replace_future.GetCallback());
  ASSERT_TRUE(replace_future.Get().success);

  base::test::TestFuture<AdBlockDecision> decision_future;
  service.CheckRequest(ScriptRequest("chrome://settings/app.js"),
                       decision_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kAllow, decision_future.Get().action);
  EXPECT_EQ(0u, service.stats()->total_blocked_count());
}

TEST_F(AdBlockAsyncTest, ServiceRetainsBoundedBlockedNavigationDetails) {
  AdBlockService service(nullptr);
  base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
  service.ReplaceRulesForTesting(RuleBytes("||blocked.example^$document\n"),
                                 replace_future.GetCallback());
  ASSERT_TRUE(replace_future.Get().success);

  base::test::TestFuture<AdBlockDecision> decision_future;
  AdBlockRequest request = BuildNavigationAdBlockRequest(
      GURL("https://blocked.example/path"), std::nullopt, "GET");
  request.mode = AdBlockMode::kAggressive;
  service.CheckRequest(std::move(request), decision_future.GetCallback());
  EXPECT_EQ(AdBlockAction::kBlock, decision_future.Get().action);
  ASSERT_TRUE(service.last_blocked_navigation());
  EXPECT_EQ("https://blocked.example/path",
            service.last_blocked_navigation()->url);
  EXPECT_EQ(AdBlockAction::kBlock,
            service.last_blocked_navigation()->decision.action);
}

TEST_F(AdBlockAsyncTest, CosmeticResourcesRespectModeAndEngineGroup) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> default_replace;
  host.ReplaceRules(AdBlockEngineGroup::kDefault,
                    RuleBytes("news.example##.default-ad\n"
                              "news.example##.card:has(.sponsor)\n"
                              "news.example##.default-procedural:"
                              "has-text(Promoted)\n"),
                    default_replace.GetCallback());
  ASSERT_TRUE(default_replace.Get().success);

  base::test::TestFuture<AdBlockEngineReplaceResult> additional_replace;
  host.ReplaceRules(AdBlockEngineGroup::kAdditional,
                    RuleBytes("news.example##.additional-ad\n"
                              "news.example##.additional-procedural:"
                              "has-text(Sponsored)\n"),
                    additional_replace.GetCallback());
  ASSERT_TRUE(additional_replace.Get().success);

  base::test::TestFuture<AdBlockCosmeticResources> standard_future;
  host.GetCosmeticResources("https://news.example/article",
                            AdBlockMode::kStandard,
                            standard_future.GetCallback());
  const AdBlockCosmeticResources standard = standard_future.Take();
  EXPECT_TRUE(standard.enabled);
  EXPECT_EQ(std::vector<std::string>({".default-ad"}),
            standard.default_rules.selectors);
  EXPECT_EQ(std::vector<std::string>({".additional-ad"}),
            standard.additional_rules.selectors);
  EXPECT_TRUE(standard.default_rules.procedural_actions.empty());
  EXPECT_EQ(1u, standard.additional_rules.procedural_actions.size());

  base::test::TestFuture<AdBlockCosmeticResources> aggressive_future;
  host.GetCosmeticResources("https://news.example/article",
                            AdBlockMode::kAggressive,
                            aggressive_future.GetCallback());
  const AdBlockCosmeticResources aggressive = aggressive_future.Take();
  EXPECT_NE(
      std::ranges::find(aggressive.default_rules.selectors, ".default-ad"),
      aggressive.default_rules.selectors.end());
  EXPECT_NE(std::ranges::find(aggressive.default_rules.selectors,
                              ".card:has(.sponsor)"),
            aggressive.default_rules.selectors.end());
  EXPECT_EQ(std::vector<std::string>({".additional-ad"}),
            aggressive.additional_rules.selectors);
  EXPECT_EQ(1u, aggressive.default_rules.procedural_actions.size());
  EXPECT_EQ(1u, aggressive.additional_rules.procedural_actions.size());
}

TEST_F(AdBlockAsyncTest, CspDirectivesCombineEnginesAndRespectOffMode) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> default_replace;
  host.ReplaceRules(AdBlockEngineGroup::kDefault,
                    RuleBytes("||news.example^$csp=script-src 'self'\n"),
                    default_replace.GetCallback());
  ASSERT_TRUE(default_replace.Get().success);

  base::test::TestFuture<AdBlockEngineReplaceResult> additional_replace;
  host.ReplaceRules(AdBlockEngineGroup::kAdditional,
                    RuleBytes("||news.example^$csp=frame-src 'none'\n"),
                    additional_replace.GetCallback());
  ASSERT_TRUE(additional_replace.Get().success);

  AdBlockRequest request("https://news.example/index.html", "news.example",
                         "news.example", "main_frame", false);

  // Both engines contribute; appending policies can only further restrict.
  base::test::TestFuture<std::string> standard;
  host.GetCspDirectives(request, AdBlockMode::kStandard,
                        standard.GetCallback());
  const std::string combined = standard.Take();
  EXPECT_NE(combined.find("script-src 'self'"), std::string::npos);
  EXPECT_NE(combined.find("frame-src 'none'"), std::string::npos);

  // Off must never inject a policy.
  base::test::TestFuture<std::string> off;
  host.GetCspDirectives(request, AdBlockMode::kOff, off.GetCallback());
  EXPECT_EQ(off.Take(), "");
}

TEST_F(AdBlockAsyncTest, CspDirectivesEmptyForNonMatchingNavigation) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> replace;
  host.ReplaceRuleSets(RuleBytes("||news.example^$csp=script-src 'self'\n"),
                       RuleBytes(""), replace.GetCallback());
  ASSERT_TRUE(replace.Get().success);

  base::test::TestFuture<std::string> future;
  host.GetCspDirectives(
      AdBlockRequest("https://other.example/index.html", "other.example",
                     "other.example", "main_frame", false),
      AdBlockMode::kStandard, future.GetCallback());
  EXPECT_EQ(future.Take(), "");
}

TEST_F(AdBlockAsyncTest, DynamicCosmeticsMergeExceptionsAcrossEngineGroups) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> default_replace;
  host.ReplaceRules(AdBlockEngineGroup::kDefault,
                    RuleBytes("##.shared-ad\n"
                              "news.example#@#.shared-ad\n"),
                    default_replace.GetCallback());
  ASSERT_TRUE(default_replace.Get().success);

  base::test::TestFuture<AdBlockEngineReplaceResult> additional_replace;
  host.ReplaceRules(AdBlockEngineGroup::kAdditional,
                    RuleBytes("##.shared-ad\n"),
                    additional_replace.GetCallback());
  ASSERT_TRUE(additional_replace.Get().success);

  base::test::TestFuture<AdBlockDynamicCosmeticSelectors> selectors_future;
  host.GetDynamicCosmeticSelectors("https://news.example/article",
                                   AdBlockMode::kStandard, {"shared-ad"}, {},
                                   selectors_future.GetCallback());
  const AdBlockDynamicCosmeticSelectors selectors = selectors_future.Take();
  EXPECT_TRUE(selectors.default_selectors.empty());
  EXPECT_TRUE(selectors.additional_selectors.empty());
}

TEST_F(AdBlockAsyncTest, GenerichideFromEitherEngineStopsGenericDiscovery) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockEngineReplaceResult> default_replace;
  host.ReplaceRules(AdBlockEngineGroup::kDefault,
                    RuleBytes("@@||news.example^$generichide\n"),
                    default_replace.GetCallback());
  ASSERT_TRUE(default_replace.Get().success);

  base::test::TestFuture<AdBlockEngineReplaceResult> additional_replace;
  host.ReplaceRules(AdBlockEngineGroup::kAdditional,
                    RuleBytes("##.generic-ad\n"),
                    additional_replace.GetCallback());
  ASSERT_TRUE(additional_replace.Get().success);

  base::test::TestFuture<AdBlockCosmeticResources> resources_future;
  host.GetCosmeticResources("https://news.example/article",
                            AdBlockMode::kStandard,
                            resources_future.GetCallback());
  const AdBlockCosmeticResources resources = resources_future.Take();
  EXPECT_FALSE(resources.default_rules.query_generics);
  EXPECT_FALSE(resources.additional_rules.query_generics);

  base::test::TestFuture<AdBlockDynamicCosmeticSelectors> selectors_future;
  host.GetDynamicCosmeticSelectors("https://news.example/article",
                                   AdBlockMode::kStandard, {"generic-ad"}, {},
                                   selectors_future.GetCallback());
  const AdBlockDynamicCosmeticSelectors selectors = selectors_future.Take();
  EXPECT_TRUE(selectors.default_selectors.empty());
  EXPECT_TRUE(selectors.additional_selectors.empty());
}

TEST_F(AdBlockAsyncTest, OffModeDisablesAllCosmeticWork) {
  AdBlockEngineHost host;
  base::test::TestFuture<AdBlockCosmeticResources> resources_future;
  host.GetCosmeticResources("https://news.example/article", AdBlockMode::kOff,
                            resources_future.GetCallback());
  EXPECT_FALSE(resources_future.Get().enabled);

  base::test::TestFuture<AdBlockDynamicCosmeticSelectors> selectors_future;
  host.GetDynamicCosmeticSelectors("https://news.example/article",
                                   AdBlockMode::kOff, {"ad"}, {"banner"},
                                   selectors_future.GetCallback());
  const AdBlockDynamicCosmeticSelectors selectors = selectors_future.Take();
  EXPECT_TRUE(selectors.default_selectors.empty());
  EXPECT_TRUE(selectors.additional_selectors.empty());
}

TEST(AdBlockStatsServiceTest, BoundsTrackedFrameState) {
  AdBlockStatsService stats;
  stats.RecordBlocked(std::nullopt);
  EXPECT_EQ(1u, stats.total_blocked_count());
  EXPECT_EQ(0u, stats.tracked_frame_count_for_testing());
}

}  // namespace
}  // namespace seoul::adblock
