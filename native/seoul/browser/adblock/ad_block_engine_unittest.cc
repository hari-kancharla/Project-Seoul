// Project Seoul native blocker engine tests.

#include "seoul/browser/adblock/ad_block_engine.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

std::unique_ptr<AdBlockEngine> Build(std::string_view rules) {
  std::string error;
  std::unique_ptr<AdBlockEngine> engine =
      AdBlockEngine::Create(base::as_byte_span(rules), &error);
  EXPECT_TRUE(engine) << error;
  return engine;
}

AdBlockRequest ScriptRequest(std::string url,
                             std::string hostname,
                             std::string source_hostname,
                             bool is_third_party) {
  return AdBlockRequest(std::move(url), std::move(hostname),
                        std::move(source_hostname), "script", is_third_party);
}

TEST(AdBlockEngineTest, BlocksMatchingNetworkRequest) {
  auto engine = Build("||ads.example^\n");
  ASSERT_TRUE(engine);

  const AdBlockMatchResult result = engine->Evaluate(ScriptRequest(
      "https://ads.example/banner.js", "ads.example", "news.example", true));
  EXPECT_TRUE(result.matched);
  EXPECT_FALSE(result.has_exception);
  ASSERT_TRUE(result.matched_rule);
  EXPECT_NE(result.matched_rule->find("ads.example"), std::string::npos);
}

TEST(AdBlockEngineTest, AppliesResourceTypeAndDomainConstraints) {
  auto engine = Build("||cdn.example/tracker.js$script,domain=news.example\n");
  ASSERT_TRUE(engine);

  EXPECT_TRUE(engine
                  ->Evaluate(ScriptRequest("https://cdn.example/tracker.js",
                                           "cdn.example", "news.example", true))
                  .matched);

  AdBlockRequest image = ScriptRequest("https://cdn.example/tracker.js",
                                       "cdn.example", "news.example", true);
  image.request_type = "image";
  EXPECT_FALSE(engine->Evaluate(image).matched);
  EXPECT_FALSE(
      engine
          ->Evaluate(ScriptRequest("https://cdn.example/tracker.js",
                                   "cdn.example", "different.example", true))
          .matched);
}

TEST(AdBlockEngineTest, AppliesThirdPartyConstraint) {
  auto engine = Build("||metrics.example^$third-party\n");
  ASSERT_TRUE(engine);

  EXPECT_TRUE(
      engine
          ->Evaluate(ScriptRequest("https://metrics.example/pixel.js",
                                   "metrics.example", "news.example", true))
          .matched);
  EXPECT_FALSE(
      engine
          ->Evaluate(ScriptRequest("https://metrics.example/pixel.js",
                                   "metrics.example", "metrics.example", false))
          .matched);
}

TEST(AdBlockEngineTest, ExceptionOverridesBlockingRule) {
  auto engine = Build("||ads.example^\n@@||ads.example/allowed.js$script\n");
  ASSERT_TRUE(engine);

  const AdBlockMatchResult allowed = engine->Evaluate(ScriptRequest(
      "https://ads.example/allowed.js", "ads.example", "news.example", true));
  EXPECT_FALSE(allowed.matched);
  EXPECT_TRUE(allowed.has_exception);
  ASSERT_TRUE(allowed.exception_rule);
  EXPECT_NE(allowed.exception_rule->find("allowed.js"), std::string::npos);

  EXPECT_TRUE(engine
                  ->Evaluate(ScriptRequest("https://ads.example/blocked.js",
                                           "ads.example", "news.example", true))
                  .matched);
}

TEST(AdBlockEngineTest, ImportantRuleOverridesException) {
  auto engine = Build(
      "||ads.example/forced.js$important\n"
      "@@||ads.example/forced.js\n");
  ASSERT_TRUE(engine);

  const AdBlockMatchResult result = engine->Evaluate(ScriptRequest(
      "https://ads.example/forced.js", "ads.example", "news.example", true));
  EXPECT_TRUE(result.matched);
  EXPECT_TRUE(result.important);
  EXPECT_FALSE(result.has_exception);
}

TEST(AdBlockEngineTest, ResolvesOnlyBundledRedirectResources) {
  auto engine = Build(
      "||ads.example/noop.js$redirect=noopjs\n"
      "||ads.example/unknown.js$redirect=not-registered.js\n");
  ASSERT_TRUE(engine);

  const AdBlockMatchResult vetted = engine->Evaluate(ScriptRequest(
      "https://ads.example/noop.js", "ads.example", "news.example", true));
  EXPECT_TRUE(vetted.matched);
  ASSERT_TRUE(vetted.redirect);
  EXPECT_EQ(
      "data:application/javascript;base64,"
      "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKfSkoKTsK",
      *vetted.redirect);

  const AdBlockMatchResult unknown = engine->Evaluate(ScriptRequest(
      "https://ads.example/unknown.js", "ads.example", "news.example", true));
  EXPECT_TRUE(unknown.matched);
  EXPECT_FALSE(unknown.redirect);
}

TEST(AdBlockEngineTest, ProducesRemoveParameterRewriteCandidate) {
  auto engine = Build("*$removeparam=utm\n");
  ASSERT_TRUE(engine);

  AdBlockRequest request =
      ScriptRequest("https://news.example/article?id=8&utm=tracking",
                    "news.example", "news.example", false);
  request.request_type = "xmlhttprequest";
  const AdBlockMatchResult result = engine->Evaluate(request);
  EXPECT_FALSE(result.matched);
  ASSERT_TRUE(result.rewritten_url);
  EXPECT_EQ("https://news.example/article?id=8", *result.rewritten_url);
}

TEST(AdBlockEngineTest, BadfilterDisablesEquivalentRule) {
  auto engine = Build(
      "||disabled.example^$script\n"
      "||disabled.example^$script,badfilter\n");
  ASSERT_TRUE(engine);

  EXPECT_FALSE(
      engine
          ->Evaluate(ScriptRequest("https://disabled.example/tracker.js",
                                   "disabled.example", "news.example", true))
          .matched);
}

TEST(AdBlockEngineTest, SerializedEngineRoundTrips) {
  auto engine = Build("||ads.example^\n");
  ASSERT_TRUE(engine);
  const std::vector<uint8_t> serialized = engine->Serialize();
  ASSERT_FALSE(serialized.empty());

  auto restored = Build("");
  ASSERT_TRUE(restored);
  ASSERT_TRUE(restored->Deserialize(serialized));
  EXPECT_TRUE(restored
                  ->Evaluate(ScriptRequest("https://ads.example/banner.js",
                                           "ads.example", "news.example", true))
                  .matched);
}

TEST(AdBlockEngineTest, RejectsInvalidUtf8WithoutCrashing) {
  const std::vector<uint8_t> invalid_utf8 = {0xff, 0xfe};
  std::string error;
  EXPECT_FALSE(AdBlockEngine::Create(invalid_utf8, &error));
  EXPECT_FALSE(error.empty());
}

TEST(AdBlockEngineTest, AllowsInvalidUtf8RequestMetadataWithoutCrashing) {
  auto engine = Build("||ads.example^\n");
  ASSERT_TRUE(engine);

  std::string invalid_url = "https://ads.example/";
  invalid_url.push_back(static_cast<char>(0xff));
  const AdBlockMatchResult result = engine->Evaluate(ScriptRequest(
      std::move(invalid_url), "ads.example", "news.example", true));
  EXPECT_FALSE(result.matched);
  EXPECT_FALSE(result.has_exception);
}

TEST(AdBlockEngineTest, IgnoresMalformedRuleWithoutFalseMatch) {
  auto engine = Build("||valid.example^\n/$this[is(not-valid/\n");
  ASSERT_TRUE(engine);

  EXPECT_FALSE(
      engine
          ->Evaluate(ScriptRequest("https://unrelated.example/app.js",
                                   "unrelated.example", "news.example", true))
          .matched);
  EXPECT_TRUE(
      engine
          ->Evaluate(ScriptRequest("https://valid.example/app.js",
                                   "valid.example", "news.example", true))
          .matched);
}

TEST(AdBlockEngineTest, ReturnsOnlyUrlSpecificCssCosmeticResources) {
  auto engine = Build(
      "news.example##.sponsored\n"
      "news.example#@#.excepted\n"
      "news.example##+js(abort-on-property-read, tracking)\n");
  ASSERT_TRUE(engine);

  const AdBlockCosmeticEngineResources resources =
      engine->GetUrlCosmeticResources("https://news.example/article");
  EXPECT_EQ(std::vector<std::string>({".sponsored"}), resources.hide_selectors);
  EXPECT_EQ(std::vector<std::string>({".excepted"}), resources.exceptions);
  EXPECT_FALSE(resources.generichide);
  EXPECT_TRUE(resources.isolated_script.empty());
}

TEST(AdBlockEngineTest, ReturnsOnlyBrowserVettedScriptletCode) {
  auto engine = Build(
      "news.example##+js(remove-elements, .sponsored)\n"
      "news.example##+js(not-registered, body)\n");
  ASSERT_TRUE(engine);

  const AdBlockCosmeticEngineResources resources =
      engine->GetUrlCosmeticResources("https://news.example/article");
  EXPECT_NE(resources.isolated_script.find("function seoulRemoveElements"),
            std::string::npos);
  EXPECT_NE(
      resources.isolated_script.find("seoulRemoveElements(\".sponsored\")"),
      std::string::npos);
  EXPECT_EQ(resources.isolated_script.find("not-registered"),
            std::string::npos);
}

TEST(AdBlockEngineTest, ReturnsStructuredProceduralActionsAsData) {
  auto engine = Build(
      "news.example##.sponsored:has-text(Promoted)\n"
      "news.example##.overlay:remove()\n");
  ASSERT_TRUE(engine);

  const AdBlockCosmeticEngineResources resources =
      engine->GetUrlCosmeticResources("https://news.example/article");
  ASSERT_EQ(2u, resources.procedural_actions.size());
  EXPECT_NE(resources.procedural_actions[0].find("\"selector\""),
            std::string::npos);
  EXPECT_NE(resources.procedural_actions[0].find("\"type\""),
            std::string::npos);
  EXPECT_NE(resources.procedural_actions[1].find("\"selector\""),
            std::string::npos);
}

TEST(AdBlockEngineTest, ResolvesGenericClassAndIdSelectorsWithExceptions) {
  auto engine = Build(
      "##.generic-ad\n"
      "###generic-banner\n"
      "news.example#@#.generic-ad\n");
  ASSERT_TRUE(engine);

  const AdBlockCosmeticEngineResources resources =
      engine->GetUrlCosmeticResources("https://news.example/article");
  EXPECT_EQ(std::vector<std::string>({".generic-ad"}), resources.exceptions);

  EXPECT_EQ(std::vector<std::string>({"#generic-banner"}),
            engine->GetHiddenClassIdSelectors(
                {"generic-ad"}, {"generic-banner"}, resources.exceptions));
}

TEST(AdBlockEngineTest, GenerichideDisablesGenericDiscoveryForSite) {
  auto engine = Build(
      "##.generic-ad\n"
      "@@||news.example^$generichide\n");
  ASSERT_TRUE(engine);

  const AdBlockCosmeticEngineResources resources =
      engine->GetUrlCosmeticResources("https://news.example/article");
  EXPECT_TRUE(resources.generichide);
}


// $csp support. The engine resolves exceptions and merges multiple matching
// directives itself; Seoul only ever appends the result as an extra policy.
AdBlockRequest DocumentRequest(std::string url,
                               std::string hostname,
                               std::string source_hostname,
                               bool is_third_party) {
  return AdBlockRequest(std::move(url), std::move(hostname),
                        std::move(source_hostname), "document",
                        is_third_party);
}

TEST(AdBlockEngineTest, ReturnsCspDirectiveForMatchingDocument) {
  auto engine = Build("||news.example^$csp=script-src 'self'\n");
  ASSERT_TRUE(engine);

  EXPECT_EQ(engine->GetCspDirectives(DocumentRequest(
                "https://news.example/index.html", "news.example",
                "news.example", false)),
            "script-src 'self'");
}

TEST(AdBlockEngineTest, CspExceptionSuppressesInjection) {
  auto engine = Build(
      "||news.example^$csp=script-src 'self'\n"
      "@@||news.example^$csp\n");
  ASSERT_TRUE(engine);

  EXPECT_EQ(engine->GetCspDirectives(DocumentRequest(
                "https://news.example/index.html", "news.example",
                "news.example", false)),
            "");
}

TEST(AdBlockEngineTest, CombinesMultipleMatchingCspDirectives) {
  auto engine = Build(
      "||news.example^$csp=script-src 'self'\n"
      "||news.example^$csp=frame-src 'none'\n");
  ASSERT_TRUE(engine);

  const std::string directives = engine->GetCspDirectives(DocumentRequest(
      "https://news.example/index.html", "news.example", "news.example",
      false));
  EXPECT_NE(directives.find("script-src 'self'"), std::string::npos);
  EXPECT_NE(directives.find("frame-src 'none'"), std::string::npos);
}

TEST(AdBlockEngineTest, NoCspDirectiveForNonMatchingOrSubresourceRequests) {
  auto engine = Build("||news.example^$csp=script-src 'self'\n");
  ASSERT_TRUE(engine);

  // Non-matching document.
  EXPECT_EQ(engine->GetCspDirectives(DocumentRequest(
                "https://other.example/index.html", "other.example",
                "other.example", false)),
            "");
  // Matching host, but a subresource request type never carries $csp.
  EXPECT_EQ(engine->GetCspDirectives(ScriptRequest("https://news.example/a.js",
                                                   "news.example",
                                                   "news.example", false)),
            "");
}

TEST(AdBlockEngineTest, AppliesCspToSubdocumentRequests) {
  auto engine = Build("||frames.example^$csp=script-src 'none'\n");
  ASSERT_TRUE(engine);

  AdBlockRequest subdocument = DocumentRequest("https://frames.example/f.html",
                                               "frames.example",
                                               "top.example", true);
  subdocument.request_type = "subdocument";
  EXPECT_EQ(engine->GetCspDirectives(subdocument), "script-src 'none'");
}

TEST(AdBlockEngineTest, MalformedCspRuleDoesNotYieldDirectives) {
  // A `$csp` option with no value on a blocking rule is not a usable policy.
  auto engine = Build("||news.example^$csp\n");
  ASSERT_TRUE(engine);

  EXPECT_EQ(engine->GetCspDirectives(DocumentRequest(
                "https://news.example/index.html", "news.example",
                "news.example", false)),
            "");
}

}  // namespace
}  // namespace seoul::adblock
