// Project Seoul native blocker browser-process integration tests.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "base/test/run_until.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "seoul/browser/adblock/ad_block_engine_host.h"
#include "seoul/browser/adblock/ad_block_resource_catalog.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_stats_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

std::vector<uint8_t> RuleBytes(std::string_view rules) {
  return std::vector<uint8_t>(rules.begin(), rules.end());
}

class AdBlockBrowserTest : public InProcessBrowserTest {
 public:
  AdBlockBrowserTest() = default;
  ~AdBlockBrowserTest() override = default;

 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AdBlockBrowserTest::HandleRequest, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void ReplaceRules(std::string_view rules) {
    AdBlockService* service =
        AdBlockServiceFactory::GetForProfile(browser()->profile());
    ASSERT_TRUE(service);
    base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
    service->ReplaceRulesForTesting(RuleBytes(rules),
                                    replace_future.GetCallback());
    ASSERT_TRUE(replace_future.Get().success);
  }

  void ReplaceAdditionalRules(std::string_view rules) {
    AdBlockService* service =
        AdBlockServiceFactory::GetForProfile(browser()->profile());
    ASSERT_TRUE(service);
    base::test::TestFuture<AdBlockEngineReplaceResult> replace_future;
    service->ReplaceAdditionalRulesForTesting(RuleBytes(rules),
                                              replace_future.GetCallback());
    ASSERT_TRUE(replace_future.Get().success);
  }

  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    if (request.relative_url == "/player.html") {
      // A synthetic player in the video.js ads convention: an ad-state marker
      // and a skip control that records presses. Real enough for the player-ad
      // treatment, with none of a real ad server's nondeterminism.
      response->set_content_type("text/html");
      response->set_content(
          "<html><body>"
          "<div class=\"vjs-ad-playing\">"
          "  <video muted></video>"
          "  <button class=\"vjs-skip-button\" "
          "onclick=\"window.__skips=(window.__skips||0)+1\">Skip</button>"
          "</div>"
          "<script>window.__skips=0;</script>"
          "</body></html>");
      return response;
    }
    if (request.relative_url == "/page.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<script>window.allowedScriptRan=false;"
          "window.blockedScriptRan=false;</script>"
          "<script src=\"" +
          embedded_test_server()->GetURL("ads.example", "/blocked.js").spec() +
          "\"></script>"
          "<script src=\"/allowed.js\"></script>");
      return response;
    }
    // ---- `$csp` fixtures -------------------------------------------------
    // Inline script sets a flag. Under `script-src 'none'` it must not run.
    // EvalJs reads the flag over the DevTools protocol, which is not subject to
    // the page's CSP, so the flag is observable either way.
    if (request.relative_url == "/csp.html") {
      response->set_content_type("text/html");
      response->set_content("<script>window.scriptRan = true;</script>ok");
      return response;
    }
    // Site ships its own policy; the blocker must not disturb it.
    if (request.relative_url == "/csp_existing.html") {
      response->set_content_type("text/html");
      response->AddCustomHeader("Content-Security-Policy", "img-src 'none'");
      response->set_content(
          "<script>window.scriptRan = true;</script>"
          "<img id=\"img\" src=\"/image.png\">");
      return response;
    }
    if (request.relative_url == "/csp_report_only.html") {
      response->set_content_type("text/html");
      response->AddCustomHeader("Content-Security-Policy-Report-Only",
                                "script-src 'none'");
      response->set_content("<script>window.scriptRan = true;</script>ok");
      return response;
    }
    if (request.relative_url == "/csp_parent.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<script>window.scriptRan = true;</script>"
          "<iframe id=\"child\" src=\"" +
          embedded_test_server()->GetURL("frame.test", "/csp.html").spec() +
          "\"></iframe>");
      return response;
    }
    if (request.relative_url == "/csp_redirect") {
      response->set_code(net::HTTP_FOUND);
      response->AddCustomHeader(
          "Location",
          embedded_test_server()->GetURL("final.test", "/csp.html").spec());
      return response;
    }
    if (request.relative_url == "/image.png") {
      response->set_content_type("image/png");
      response->set_content("not-a-real-png-but-a-load-attempt");
      return response;
    }
    if (request.relative_url == "/cosmetic.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<div id=\"domain-ad\" class=\"domain-ad\">domain ad</div>"
          "<div id=\"dynamic-root\"></div>"
          "<script>"
          "const dynamic = document.createElement('div');"
          "dynamic.id = 'dynamic-ad';"
          "dynamic.className = 'generic-ad';"
          "dynamic.textContent = 'generic ad';"
          "document.getElementById('dynamic-root').appendChild(dynamic);"
          "</script>");
      return response;
    }
    if (request.relative_url == "/scriptlet.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<div id=\"scriptlet-ad\" class=\"scriptlet-ad\">ad</div>");
      return response;
    }
    // ---- fingerprinting fixtures --------------------------------------
    // A page that loads nothing else, so what a probe reads back is the
    // probe's own drawing and nothing the blocker did to the page.
    if (request.relative_url == "/farble.html") {
      response->set_content_type("text/html");
      response->set_content("<title>farble</title>");
      return response;
    }
    // A dedicated worker draws the canvas probe's scene on an OffscreenCanvas
    // and reads it back both ways a worker can, and reports its navigator's
    // hardware profile beside the digests.
    if (request.relative_url == "/farble-worker.js") {
      response->set_content_type("application/javascript");
      response->set_content(
          "(async () => {"
          " const digest = bytes => { let sum = 0;"
          "  for (let i = 0; i < bytes.length; ++i) {"
          "   sum = (sum * 31 + bytes[i]) >>> 0; }"
          "  return String(sum); };"
          " const canvas = new OffscreenCanvas(64, 32);"
          " const context = canvas.getContext('2d', {willReadFrequently: true});"
          " context.fillStyle = '#a1b2c3'; context.fillRect(0, 0, 64, 32);"
          " context.fillStyle = '#102030'; context.font = '16px sans-serif';"
          " context.fillText('Seoul', 4, 20);"
          " const sum = digest(context.getImageData(0, 0, 64, 32).data);"
          " const blob = await canvas.convertToBlob();"
          " const blobSum = digest(new Uint8Array(await blob.arrayBuffer()));"
          " postMessage({sum: sum, blobSum: blobSum,"
          "  cores: navigator.hardwareConcurrency,"
          "  memory: navigator.deviceMemory === undefined ? -1 :"
          "   navigator.deviceMemory});"
          "})().catch(e => postMessage({error: String(e)}));");
      return response;
    }
    if (request.relative_url == "/scriptlet-library.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<video id=\"player\" autoplay data-track=\"beacon-42\"></video>"
          "<div id=\"badge\" class=\"sponsored plain\">promo</div>");
      return response;
    }
    if (request.relative_url == "/ga-consumer.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<script src=\"ga.js\"></script>"
          "<script>"
          "window.gaOutcome = 'missing';"
          "try {"
          "  window._gaq.push(['_setAccount', 'UA-0']);"
          "  window._gaq.push(function() { window.gaOutcome = 'stubbed'; });"
          "} catch (e) { window.gaOutcome = 'threw:' + e.name; }"
          "</script>");
      return response;
    }
    if (request.relative_url == "/procedural.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<div id=\"procedural-match\" class=\"sponsored\">"
          "Promoted offer</div>"
          "<div id=\"procedural-unmatched\" class=\"sponsored\">"
          "Editorial article</div>"
          "<div id=\"procedural-remove\" class=\"remove-me\">overlay</div>"
          "<script>"
          "const dynamic = document.createElement('div');"
          "dynamic.id = 'procedural-dynamic';"
          "dynamic.className = 'sponsored';"
          "dynamic.textContent = 'Promoted later';"
          "document.body.appendChild(dynamic);"
          "</script>");
      return response;
    }
    if (request.relative_url == "/frame-host.html") {
      response->set_content_type("text/html");
      response->set_content("<iframe id=\"ad-frame\" src=\"" +
                            embedded_test_server()
                                ->GetURL("frame.example", "/frame-content.html")
                                .spec() +
                            "\"></iframe>");
      return response;
    }
    if (request.relative_url == "/frame-content.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<div id=\"frame-ad\" class=\"frame-ad\">frame ad</div>");
      return response;
    }
    if (request.relative_url == "/subdocument-host.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<script>window.hostLoaded = true;</script><iframe id=\"ad\" "
          "src=\"" +
          embedded_test_server()->GetURL("ads.example", "/ad-frame.html").spec() +
          "\"></iframe><iframe id=\"ok\" src=\"" +
          embedded_test_server()
              ->GetURL("widgets.example", "/ok-frame.html")
              .spec() +
          "\"></iframe>");
      return response;
    }
    if (request.relative_url == "/ad-frame.html") {
      ++ad_frame_requests_;
      response->set_content_type("text/html");
      response->set_content("<script>window.adFrameLoaded = true;</script>ad");
      return response;
    }
    if (request.relative_url == "/ok-frame.html") {
      ++ok_frame_requests_;
      response->set_content_type("text/html");
      response->set_content("<script>window.okFrameLoaded = true;</script>ok");
      return response;
    }
    if (request.relative_url == "/other.html") {
      response->set_content_type("text/html");
      response->set_content("<p>other page</p>");
      return response;
    }
    if (request.relative_url == "/network-transform.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<script>"
          "window.transformDone = Promise.all(["
          " fetch('/redirect-me.js').then(r => r.text()),"
          " fetch('/api?keep=1&utm=tracking').then(r => r.text())"
          "]).then(values => {"
          " window.redirectBody = values[0];"
          " window.rewriteBody = values[1];"
          " return true;"
          "});"
          "</script>");
      return response;
    }
    if (request.relative_url == "/redirect-me.js") {
      ++redirect_resource_requests_;
      response->set_content_type("application/javascript");
      response->set_content("network response must not be used");
      return response;
    }
    if (request.relative_url == "/api?keep=1&utm=tracking") {
      ++unsafe_original_rewrite_requests_;
      response->set_content_type("text/plain");
      response->set_content(request.relative_url);
      return response;
    }
    if (request.relative_url == "/api?keep=1") {
      ++rewritten_requests_;
      response->set_content_type("text/plain");
      response->set_content(request.relative_url);
      return response;
    }
    if (request.relative_url == "/rewrite-nav?keep=1&utm=tracking") {
      ++unsafe_original_navigation_requests_;
      response->set_content_type("text/html");
      response->set_content("unstripped navigation");
      return response;
    }
    if (request.relative_url == "/rewrite-nav?keep=1") {
      ++rewritten_navigation_requests_;
      response->set_content_type("text/html");
      response->set_content(
          "<script>window.finalSearch=location.search;</script>");
      return response;
    }
    if (request.relative_url == "/blocked.js") {
      ++blocked_script_requests_;
      response->set_content_type("application/javascript");
      response->set_content("window.blockedScriptRan=true;");
      return response;
    }
    if (request.relative_url == "/allowed.js") {
      ++allowed_script_requests_;
      response->set_content_type("application/javascript");
      response->set_content("window.allowedScriptRan=true;");
      return response;
    }
    if (request.relative_url == "/socket") {
      ++websocket_requests_;
      response->set_code(net::HTTP_BAD_REQUEST);
      return response;
    }
    if (request.relative_url == "/blocked-navigation") {
      ++blocked_navigation_requests_;
      response->set_content_type("text/html");
      response->set_content("must not arrive");
      return response;
    }
    return nullptr;
  }

  std::atomic<int> blocked_script_requests_{0};
  std::atomic<int> allowed_script_requests_{0};
  std::atomic<int> websocket_requests_{0};
  std::atomic<int> blocked_navigation_requests_{0};
  std::atomic<int> redirect_resource_requests_{0};
  std::atomic<int> rewritten_requests_{0};
  std::atomic<int> unsafe_original_rewrite_requests_{0};
  std::atomic<int> rewritten_navigation_requests_{0};
  std::atomic<int> unsafe_original_navigation_requests_{0};
  std::atomic<int> ad_frame_requests_{0};
  std::atomic<int> ok_frame_requests_{0};
};

// A third-party ad iframe is the most visible ad format on the web, and the
// one class Seoul never filtered: navigation loads are not proxied, and the
// throttle declined every subframe, so no `$subdocument` rule could ever fire.
// The rule must stop the frame before the server sees it, collapse the empty
// box it would leave, and touch nothing else on the page.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       BlocksThirdPartyAdSubframeAndCollapsesIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ReplaceRules("||ads.example^$subdocument\n");

  const GURL host_url =
      embedded_test_server()->GetURL("news.example", "/subdocument-host.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), host_url));
  content::WebContents* const contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);

  // The embedding page is untouched.
  EXPECT_EQ(host_url, contents->GetLastCommittedURL());
  EXPECT_EQ(true, content::EvalJs(contents, "window.hostLoaded === true"));

  // The ad frame never reached the network.
  EXPECT_EQ(0, ad_frame_requests_.load())
      << "the ad iframe's document load was not filtered";

  // And it leaves no reserved gap where the ad would have been.
  EXPECT_EQ(0, content::EvalJs(
                   contents, "document.getElementById('ad').clientHeight"));

  // An unrelated third-party frame still loads - an over-broad sub_frame path
  // shows up right here.
  EXPECT_EQ(1, ok_frame_requests_.load());
}

// The same rule must not touch a top-level navigation: a main frame is first
// party to itself, and `$subdocument` does not describe it.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       SubdocumentRuleLeavesTopLevelNavigationAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ReplaceRules("||ads.example^$subdocument\n");

  const GURL direct =
      embedded_test_server()->GetURL("ads.example", "/ad-frame.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), direct));
  content::WebContents* const contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(direct, contents->GetLastCommittedURL());
  EXPECT_EQ(true, content::EvalJs(contents, "window.adFrameLoaded === true"));
  EXPECT_EQ(1, ad_frame_requests_.load());
}

// Turning blocking off for the page the user is looking at has to disable it
// for the frames inside that page too. This is what pins the subframe request
// to the *embedder* as its top frame: keyed off the frame's own URL instead,
// the per-site lookup would consult the ad network and keep blocking.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       DisablingBlockingOnTheEmbedderFreesItsSubframes) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ReplaceRules("||ads.example^$subdocument\n");

  AdBlockService* const service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL host_url =
      embedded_test_server()->GetURL("news.example", "/subdocument-host.html");
  service->SetSiteMode(host_url, AdBlockMode::kOff);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), host_url));
  EXPECT_EQ(1, ad_frame_requests_.load())
      << "blocking was off for this page, so its frames must load";
}

// The player-ad treatment rides the cosmetic pipeline into every http(s)
// document and presses a matched player's skip control. Driven through the
// REAL injection path - service to host to isolated world - against a
// synthetic player, because a live ad server decides for itself when to serve
// and a test that only sometimes has an ad only sometimes tests.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, PlayerAdTreatmentPressesSkip) {
  const GURL url = embedded_test_server()->GetURL("ads.example", "/player.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);

  // The treatment observes and polls at 500ms; wait for the press to land.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(contents, "window.__skips").ExtractInt() > 0;
  })) << "the isolated-world treatment must press the player's skip control";

  // And the page world must not see the treatment itself - the isolation is
  // the security model, so its absence here is part of the contract.
  EXPECT_EQ(false,
            content::EvalJs(contents, "!!window.__seoulPlayerAdTreatment"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       BlocksScriptBeforeEmbeddedServerReceivesIt) {
  ReplaceRules("||ads.example^$script\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.example", "/page.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(true, content::EvalJs(contents, "window.allowedScriptRan"));
  EXPECT_EQ(false, content::EvalJs(contents, "window.blockedScriptRan"));
  EXPECT_EQ(1, allowed_script_requests_.load());
  EXPECT_EQ(0, blocked_script_requests_.load());

  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  EXPECT_GE(service->stats()->total_blocked_count(), 1u);
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       BlocksWebSocketBeforeEmbeddedServerReceivesIt) {
  ReplaceRules("||ads.example^$websocket\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.example", "/page.html")));

  const GURL socket_url =
      embedded_test_server()->GetURL("ads.example", "/socket");
  GURL::Replacements websocket_scheme;
  websocket_scheme.SetSchemeStr("ws");
  const GURL websocket_url = socket_url.ReplaceComponents(websocket_scheme);
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ("blocked",
            content::EvalJs(
                contents,
                content::JsReplace("new Promise(resolve => {"
                                   " const socket = new WebSocket($1);"
                                   " socket.onopen = () => resolve('opened');"
                                   " socket.onerror = () => resolve('blocked');"
                                   "})",
                                   websocket_url)));
  EXPECT_EQ(0, websocket_requests_.load());

  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  EXPECT_GE(service->stats()->total_blocked_count(), 1u);
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       BlocksTopLevelNavigationWithBlockedByClient) {
  ReplaceRules("||blocked.example^$document\n");
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  service->SetSiteMode(embedded_test_server()->GetURL("blocked.example", "/"),
                       AdBlockMode::kAggressive);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.example", "/page.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  content::TestNavigationObserver observer(contents);
  // Chromium commits its local error page for ERR_BLOCKED_BY_CLIENT, so the
  // navigation helper itself succeeds even though the observed network
  // navigation is blocked.
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("blocked.example",
                                                "/blocked-navigation")));
  observer.Wait();

  EXPECT_FALSE(observer.last_navigation_succeeded());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, observer.last_net_error_code());
  EXPECT_EQ(0, blocked_navigation_requests_.load());

  ASSERT_TRUE(service->last_blocked_navigation());
  EXPECT_EQ(embedded_test_server()
                ->GetURL("blocked.example", "/blocked-navigation")
                .spec(),
            service->last_blocked_navigation()->url);
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       AppliesSiteModeAndTemporaryDisableToProfileRequests) {
  ReplaceRules("||news.example^$script\n");
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);

  const GURL site_url =
      embedded_test_server()->GetURL("news.example", "/page.html");
  auto check_request = [&]() {
    AdBlockRequest request(embedded_test_server()
                               ->GetURL("news.example", "/first-party.js")
                               .spec(),
                           "news.example", "news.example", "script",
                           /*is_third_party=*/false);
    request.outermost_top_frame_url = site_url.spec();
    base::test::TestFuture<AdBlockDecision> future;
    service->CheckRequest(std::move(request), future.GetCallback());
    return future.Take();
  };

  EXPECT_EQ(AdBlockAction::kAllow, check_request().action);

  service->SetSiteMode(site_url, AdBlockMode::kAggressive);
  EXPECT_EQ(AdBlockAction::kBlock, check_request().action);

  service->TemporarilyDisable(site_url, base::Hours(1));
  EXPECT_EQ(AdBlockAction::kAllow, check_request().action);

  service->ClearTemporaryDisable(site_url);
  EXPECT_EQ(AdBlockAction::kBlock, check_request().action);
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       ServesVettedRedirectAndSafelyRewritesFetch) {
  ReplaceAdditionalRules(
      "/redirect-me\\.js(?:\\?|$)/$xmlhttprequest,redirect=noopjs\n"
      "||news.example^$xmlhttprequest,removeparam=utm\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.example",
                                                "/network-transform.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(true, content::EvalJs(contents, "window.transformDone"));
  EXPECT_EQ(GetAdBlockResourceCatalog().front().body,
            content::EvalJs(contents, "window.redirectBody"));
  EXPECT_EQ("/api?keep=1", content::EvalJs(contents, "window.rewriteBody"));
  EXPECT_EQ(0, redirect_resource_requests_.load());
  EXPECT_EQ(1, rewritten_requests_.load());
  EXPECT_EQ(0, unsafe_original_rewrite_requests_.load());
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       RestartsTopLevelNavigationAfterSafeRewrite) {
  ReplaceAdditionalRules("||news.example^$document,removeparam=utm\n");
  const GURL original_url = embedded_test_server()->GetURL(
      "news.example", "/rewrite-nav?keep=1&utm=tracking");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), original_url));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(embedded_test_server()
                ->GetURL("news.example", "/rewrite-nav?keep=1")
                .spec(),
            contents->GetLastCommittedURL().spec());
  EXPECT_EQ("?keep=1", content::EvalJs(contents, "window.finalSearch"));
  EXPECT_EQ(1, rewritten_navigation_requests_.load());
  EXPECT_EQ(0, unsafe_original_navigation_requests_.load());
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       AppliesDomainAndDynamicallyDiscoveredCosmeticRules) {
  ReplaceRules(
      "news.example##.domain-ad\n"
      "##.generic-ad\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.example", "/cosmetic.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(
      "none",
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const value = getComputedStyle("
          "    document.getElementById('domain-ad')).display;"
          "  value === 'none' ? resolve(value) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
  EXPECT_EQ(
      "none",
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const value = getComputedStyle("
          "    document.getElementById('dynamic-ad')).display;"
          "  value === 'none' ? resolve(value) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       AppliesCosmeticRulesInsideCrossOriginSubframe) {
  ReplaceRules("frame.example##.frame-ad\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.example", "/frame-host.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  content::RenderFrameHost* child =
      content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  EXPECT_EQ(
      "none",
      content::EvalJs(
          child,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const value = getComputedStyle("
          "    document.getElementById('frame-ad')).display;"
          "  value === 'none' ? resolve(value) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       RunsOnlyVettedScriptletsInIsolatedWorld) {
  ReplaceRules(
      "news.example##+js(remove-elements, .scriptlet-ad)\n"
      "news.example##+js(not-registered, body)\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.example", "/scriptlet.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(
      true,
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const removed = document.getElementById('scriptlet-ad') === null;"
          "  removed ? resolve(true) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
  EXPECT_EQ("undefined",
            content::EvalJs(contents, "typeof globalThis.seoulRemoveElements"));
}

// The DOM scriptlet library is real behaviour, not catalogue entries: a
// +js(remove-attr) rule strips the attribute, +js(remove-class) strips the
// class, and both leave the rest of the element standing.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, DomScriptletLibraryActsOnThePage) {
  ReplaceRules(
      "news.example##+js(remove-attr, data-track, #player)\n"
      "news.example##+js(remove-class, sponsored, #badge)\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.example",
                                                "/scriptlet-library.html")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(true, content::EvalJs(contents,
                                  "new Promise(resolve => {"
                                  " const check = () => {"
                                  "  const player = "
                                  "document.getElementById('player');"
                                  "  const badge = "
                                  "document.getElementById('badge');"
                                  "  if (player && badge &&"
                                  "      !player.hasAttribute('data-track') &&"
                                  "      !badge.classList.contains('sponsored')"
                                  ") { resolve(true); return; }"
                                  "  requestAnimationFrame(check);"
                                  " }; check();"
                                  "})"));
  // Only the targeted parts went: the element and its other class survive.
  EXPECT_EQ(true, content::EvalJs(contents,
                                  "document.getElementById('badge')"
                                  ".classList.contains('plain')"));
}

// A $redirect to the vetted ga.js stub must keep the page's own analytics
// calling code alive: the script loads (as the stub), the legacy _gaq API
// exists, and queued callbacks fire - the whole point of shipping shims
// instead of plain blocks.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, GaRedirectStubKeepsPageCodeAlive) {
  ReplaceRules(
      "/ga\\.js(?:\\?|$)/$script,redirect=ga.js,important,domain=news.example\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.example", "/ga-consumer.html")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ("stubbed",
            content::EvalJs(contents,
                            "new Promise(resolve => {"
                            " const check = () => {"
                            "  if (window.gaOutcome === 'stubbed') {"
                            "   resolve(window.gaOutcome); return;"
                            "  }"
                            "  requestAnimationFrame(check);"
                            " }; check();"
                            "})"));
}

// ---- Fingerprinting protection --------------------------------------------
// Shared probes. Each draws the same scene and returns a digest of what a
// script would hash, so two readbacks compare byte for byte without the test
// knowing the platform's exact rasterization. Digests travel as strings
// because a 32-bit rolling sum does not fit base::Value's int.

constexpr char kCanvasProbe[] = R"((() => {
  const canvas = document.createElement('canvas');
  canvas.width = 64; canvas.height = 32;
  const context = canvas.getContext('2d', {willReadFrequently: true});
  context.fillStyle = '#a1b2c3';
  context.fillRect(0, 0, 64, 32);
  context.fillStyle = '#102030';
  context.font = '16px sans-serif';
  context.fillText('Seoul', 4, 20);
  const url1 = canvas.toDataURL();
  const url2 = canvas.toDataURL();
  let sum = 0;
  const data = context.getImageData(0, 0, 64, 32).data;
  for (let i = 0; i < data.length; ++i) { sum = (sum * 31 + data[i]) >>> 0; }
  return JSON.stringify({stable: url1 === url2, url: url1, sum: String(sum)});
})())";

// Relays whatever /farble-worker.js reports.
constexpr char kWorkerProbe[] = R"(new Promise(resolve => {
  const worker = new Worker('/farble-worker.js');
  worker.onmessage = event => {
    resolve(JSON.stringify(event.data)); worker.terminate();
  };
  worker.onerror = event => {
    resolve(JSON.stringify({error: String(event.message)})); worker.terminate();
  };
}))";

// WebGL: clear two regions and read the pixels back. Reports "<digest>:<gl
// error>", or 'unavailable' where the environment has no GL at all.
constexpr char kWebGLProbe[] = R"((() => {
  const canvas = document.createElement('canvas');
  canvas.width = 32; canvas.height = 16;
  const gl = canvas.getContext('webgl');
  if (!gl) { return 'unavailable'; }
  gl.clearColor(0.6, 0.3, 0.2, 1); gl.clear(gl.COLOR_BUFFER_BIT);
  gl.enable(gl.SCISSOR_TEST); gl.scissor(4, 4, 12, 6);
  gl.clearColor(0.1, 0.9, 0.5, 1); gl.clear(gl.COLOR_BUFFER_BIT);
  gl.disable(gl.SCISSOR_TEST);
  const pixels = new Uint8Array(32 * 16 * 4);
  gl.readPixels(0, 0, 32, 16, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
  const error = gl.getError();
  let sum = 0;
  for (let i = 0; i < pixels.length; ++i) { sum = (sum * 31 + pixels[i]) >>> 0; }
  return sum + ':' + error;
})())";

constexpr char kHardwareProbe[] = R"(JSON.stringify({
  cores: navigator.hardwareConcurrency,
  memory: navigator.deviceMemory === undefined ? -1 : navigator.deviceMemory,
}))";

base::DictValue ParseProbe(const std::string& json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(value && value->is_dict()) << json;
  return std::move(value->GetDict());
}

// Fingerprinting protection is on by default: a fresh profile farbles canvas
// readbacks on every site it governs before anyone opens the panel.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, FingerprintProtectionIsOnByDefault) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  ASSERT_EQ(FingerprintMode::kBalanced, service->GetDefaultFingerprintMode());
  const GURL site = embedded_test_server()->GetURL("a.example", "/farble.html");
  const auto probe = [&]() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
    return ParseProbe(
        content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                        kCanvasProbe)
            .ExtractString());
  };

  const base::DictValue shipped = probe();
  EXPECT_EQ(true, *shipped.FindBool("stable"))
      << "farbled readbacks are stable within a page";

  // The true pixels exist only with the default off; they are the control.
  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  const base::DictValue truth = probe();
  EXPECT_NE(*truth.FindString("url"), *shipped.FindString("url"))
      << "what ships is not the true pixels";
  EXPECT_NE(*truth.FindString("sum"), *shipped.FindString("sum"));

  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  const base::DictValue again = probe();
  EXPECT_EQ(*shipped.FindString("url"), *again.FindString("url"))
      << "the same site farbles identically for the whole session";
}

// Balanced farbling is the promise Brave makes with its shields, checked
// against real readbacks: the pixels a script reads are perturbed per site,
// stable for the session, and stand down for a site override or Shields Off.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, CanvasFarblingIsPerSiteAndStable) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL site_a =
      embedded_test_server()->GetURL("a.example", "/farble.html");
  const GURL site_b =
      embedded_test_server()->GetURL("b.example", "/farble.html");
  const auto probe = [&](const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    return ParseProbe(
        content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                        kCanvasProbe)
            .ExtractString());
  };
  const auto url_of = [](const base::DictValue& dict) {
    return *dict.FindString("url");
  };

  // The true pixels: identical drawing on both sites reads back identically
  // - that sameness is exactly what fingerprinters bank on.
  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  const base::DictValue off_a = probe(site_a);
  const base::DictValue off_b = probe(site_b);
  EXPECT_EQ(url_of(off_a), url_of(off_b));

  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  const base::DictValue farbled_a = probe(site_a);
  // Within one page, readbacks are stable: a site cannot detect farbling by
  // reading twice, and legitimate canvas round-trips keep working.
  EXPECT_EQ(true, *farbled_a.FindBool("stable"));
  // The farbled readback is not the true pixels...
  EXPECT_NE(url_of(off_a), url_of(farbled_a));
  EXPECT_NE(*off_a.FindString("sum"), *farbled_a.FindString("sum"));
  // ...and is deterministic for the site within the session.
  const base::DictValue farbled_a_again = probe(site_a);
  EXPECT_EQ(url_of(farbled_a), url_of(farbled_a_again));
  // The same drawing on another site farbles differently - the cross-site
  // link is what breaks.
  const base::DictValue farbled_b = probe(site_b);
  EXPECT_NE(url_of(farbled_a), url_of(farbled_b));

  // A site override to Off restores the true pixels for that site alone.
  service->SetSiteFingerprintMode(site_a, FingerprintMode::kOff);
  const base::DictValue site_a_off = probe(site_a);
  EXPECT_EQ(url_of(off_a), url_of(site_a_off));
  const base::DictValue site_b_untouched = probe(site_b);
  EXPECT_EQ(url_of(farbled_b), url_of(site_b_untouched));
  service->SetSiteFingerprintMode(site_a, std::nullopt);
  const base::DictValue site_a_default = probe(site_a);
  EXPECT_EQ(url_of(farbled_a), url_of(site_a_default))
      << "back on the default, the site's session pattern is unchanged";

  // Shields Off stands the farbling down.
  service->SetSiteMode(site_a, AdBlockMode::kOff);
  const base::DictValue down_a = probe(site_a);
  EXPECT_EQ(url_of(off_a), url_of(down_a));
}

// Farbling reaches every readback a script has, not only the main thread's
// 2D canvas: a dedicated worker's OffscreenCanvas (getImageData and
// convertToBlob) and WebGL readPixels return perturbed bytes under Balanced,
// stable for the session, and Strict refuses the WebGL read outright.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, FarblingCoversWorkersAndWebGL) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL site = embedded_test_server()->GetURL("a.example", "/farble.html");
  const auto navigate = [&]() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
    return browser()->tab_strip_model()->GetActiveWebContents();
  };
  const auto worker_probe = [](content::WebContents* contents) {
    return ParseProbe(content::EvalJs(contents, kWorkerProbe).ExtractString());
  };

  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  content::WebContents* contents = navigate();
  const base::DictValue worker_off = worker_probe(contents);
  ASSERT_FALSE(worker_off.contains("error")) << *worker_off.FindString("error");
  const std::string webgl_off =
      content::EvalJs(contents, kWebGLProbe).ExtractString();

  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  contents = navigate();
  const base::DictValue worker_on = worker_probe(contents);
  ASSERT_FALSE(worker_on.contains("error")) << *worker_on.FindString("error");
  EXPECT_NE(*worker_off.FindString("sum"), *worker_on.FindString("sum"))
      << "getImageData in a worker is farbled";
  EXPECT_NE(*worker_off.FindString("blobSum"), *worker_on.FindString("blobSum"))
      << "convertToBlob in a worker is farbled";
  const std::string webgl_on =
      content::EvalJs(contents, kWebGLProbe).ExtractString();

  contents = navigate();
  const base::DictValue worker_again = worker_probe(contents);
  EXPECT_EQ(*worker_on.FindString("sum"), *worker_again.FindString("sum"))
      << "and stable for the session";
  EXPECT_EQ(*worker_on.FindString("blobSum"),
            *worker_again.FindString("blobSum"));

  if (webgl_off == "unavailable" || webgl_on == "unavailable") {
    LOG(WARNING) << "WebGL is unavailable here; readPixels farbling was not "
                    "exercised by this run";
    return;
  }
  EXPECT_TRUE(base::EndsWith(webgl_off, ":0")) << webgl_off;
  EXPECT_TRUE(base::EndsWith(webgl_on, ":0")) << webgl_on;
  EXPECT_NE(webgl_off, webgl_on) << "readPixels is farbled";
  EXPECT_EQ(webgl_on, content::EvalJs(contents, kWebGLProbe).ExtractString())
      << "and stable for the session";

  // Strict refuses the read the way it refuses toDataURL: GL_INVALID_OPERATION
  // (0x0502) and a destination left untouched.
  service->SetSiteFingerprintMode(site, FingerprintMode::kStrict);
  EXPECT_EQ("0:1282", content::EvalJs(navigate(), kWebGLProbe).ExtractString());
}

// The hardware profile is farbled the way Brave farbles it: below four cores
// or 4 GiB a machine is already generic and reads true; above, each site
// sees a stable value between the floor and the truth, in a worker's
// navigator exactly as in the window's, and Strict keeps it farbled.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       HardwareProfileIsFarbledPerSiteAndStable) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  // localhost is a secure context, which deviceMemory requires.
  const GURL site = embedded_test_server()->GetURL("localhost", "/farble.html");
  content::WebContents* contents = nullptr;
  const auto probe = [&]() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
    contents = browser()->tab_strip_model()->GetActiveWebContents();
    return ParseProbe(content::EvalJs(contents, kHardwareProbe).ExtractString());
  };

  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  const base::DictValue truth = probe();
  const int real_cores = *truth.FindInt("cores");
  const double real_memory = *truth.FindDouble("memory");
  ASSERT_GE(real_cores, 1);
  ASSERT_GT(real_memory, 0.0) << "deviceMemory must be exposed on a secure context";

  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  const base::DictValue farbled = probe();
  const int cores = *farbled.FindInt("cores");
  const double memory = *farbled.FindDouble("memory");
  if (real_cores < 4) {
    EXPECT_EQ(real_cores, cores) << "a small machine is left as it is";
  } else {
    EXPECT_GE(cores, 4);
    EXPECT_LE(cores, real_cores);
  }
  if (real_memory < 4.0) {
    EXPECT_EQ(real_memory, memory);
  } else {
    EXPECT_GE(memory, 4.0);
    EXPECT_LE(memory, real_memory);
  }

  const base::DictValue again = probe();
  EXPECT_EQ(cores, *again.FindInt("cores")) << "stable across navigations";
  EXPECT_EQ(memory, *again.FindDouble("memory"));

  // One token, one profile: the worker's navigator agrees with the window's.
  const base::DictValue worker =
      ParseProbe(content::EvalJs(contents, kWorkerProbe).ExtractString());
  ASSERT_FALSE(worker.contains("error")) << *worker.FindString("error");
  EXPECT_EQ(cores, *worker.FindInt("cores"));
  EXPECT_EQ(memory, *worker.FindDouble("memory"));

  // Strict blocks the pixels; the hardware profile stays farbled, not true.
  service->SetSiteFingerprintMode(site, FingerprintMode::kStrict);
  const base::DictValue strict = probe();
  EXPECT_EQ(cores, *strict.FindInt("cores"));
  EXPECT_EQ(memory, *strict.FindDouble("memory"));
}

// Reports the readback outcome instead of throwing, for a site where Strict
// is expected to refuse it.
constexpr char kReadbackProbe[] = R"((() => {
  const canvas = document.createElement('canvas');
  canvas.width = 8; canvas.height = 8;
  const context = canvas.getContext('2d');
  context.fillStyle = '#123456';
  context.fillRect(0, 0, 8, 8);
  try { canvas.toDataURL(); return 'readable'; } catch (e) { return e.name; }
})())";

// The post-navigation preference seam starts from the previous page's values,
// so every decision is re-made and re-assigned per site, with no explicit
// recomputation anywhere in this test: a token or a taint set for one site
// must never ride into the next.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       FingerprintStateNeverRidesAcrossNavigations) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL site_a =
      embedded_test_server()->GetURL("a.example", "/farble.html");
  const GURL site_b =
      embedded_test_server()->GetURL("b.example", "/farble.html");
  const auto farbled_url = [&](const GURL& url) -> std::string {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    const base::DictValue dict = ParseProbe(
        content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                        kCanvasProbe)
            .ExtractString());
    return *dict.FindString("url");
  };
  const auto readback = [&](const GURL& url) -> std::string {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    return content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                           kReadbackProbe)
        .ExtractString();
  };

  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  const std::string truth_a = farbled_url(site_a);
  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  const std::string farbled_b = farbled_url(site_b);
  const std::string farbled_a = farbled_url(site_a);
  EXPECT_NE(farbled_a, farbled_b);

  // Off on A, reached from a farbled B: B's token must not ride along.
  service->SetSiteFingerprintMode(site_a, FingerprintMode::kOff);
  EXPECT_EQ(farbled_b, farbled_url(site_b));
  EXPECT_EQ(truth_a, farbled_url(site_a));

  // Strict on A, then B: A's taint must not ride along either.
  service->SetSiteFingerprintMode(site_a, FingerprintMode::kStrict);
  EXPECT_EQ("SecurityError", readback(site_a));
  EXPECT_EQ("readable", readback(site_b));
  EXPECT_EQ(farbled_b, farbled_url(site_b));
  EXPECT_EQ("SecurityError", readback(site_a)) << "and Strict is still Strict";

  // Back on the default, A farbles with its own session pattern again.
  service->SetSiteFingerprintMode(site_a, std::nullopt);
  EXPECT_EQ(farbled_a, farbled_url(site_a));
}

// Incognito is served by the regular profile's service but must never share
// its pattern: the same site farbles differently in an incognito window, and
// stays stable within it. Every new incognito session is a new browser
// context, and so a new pattern, by construction.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, IncognitoFarblesWithItsOwnKey) {
  const GURL site = embedded_test_server()->GetURL("a.example", "/farble.html");
  const auto farbled_url = [&](Browser* in_browser) -> std::string {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(in_browser, site));
    const base::DictValue dict = ParseProbe(
        content::EvalJs(in_browser->tab_strip_model()->GetActiveWebContents(),
                        kCanvasProbe)
            .ExtractString());
    return *dict.FindString("url");
  };

  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);

  // The private window must be PROTECTED, not merely different. Comparing it
  // against the regular window alone would pass even if incognito were farbled
  // not at all, because unfarbled pixels differ from farbled ones too. So the
  // true pixels are established first, with protection off, and the private
  // window is required to differ from those.
  service->SetDefaultFingerprintMode(FingerprintMode::kOff);
  const std::string truth = farbled_url(incognito);
  service->SetDefaultFingerprintMode(FingerprintMode::kBalanced);

  const std::string regular = farbled_url(browser());
  const std::string private_window = farbled_url(incognito);
  EXPECT_NE(truth, private_window)
      << "a private window must be farbled, not left unprotected";
  EXPECT_NE(regular, private_window);
  EXPECT_EQ(private_window, farbled_url(incognito))
      << "stable within the incognito session";
  EXPECT_EQ(regular, farbled_url(browser()))
      << "and the regular window is unaffected";
}

// The receipt: every farbled readback a page performs is counted for the
// page, by surface, attributed by the browser from the frame that reported
// it - and a new document starts a new receipt.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       FingerprintReceiptCountsEveryScrambledRead) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL site = embedded_test_server()->GetURL("a.example", "/farble.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  const content::GlobalRenderFrameHostToken page =
      contents->GetPrimaryMainFrame()->GetGlobalFrameToken();

  // Three canvas readbacks (two toDataURL, one getImageData) and one hardware
  // read; deviceMemory is not exposed on an insecure origin, so it is never
  // asked and never counted.
  ASSERT_TRUE(content::ExecJs(contents, kCanvasProbe));
  ASSERT_TRUE(content::ExecJs(contents, kHardwareProbe));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    const FarbledReadCounts receipt = service->stats()->GetFarbledReads(page);
    return receipt.canvas >= 3 && receipt.hardware >= 1;
  }));
  FarbledReadCounts receipt = service->stats()->GetFarbledReads(page);
  EXPECT_EQ(3u, receipt.canvas);
  EXPECT_EQ(1u, receipt.hardware);
  EXPECT_EQ(0u, receipt.webgl);

  const std::string webgl =
      content::EvalJs(contents, kWebGLProbe).ExtractString();
  if (webgl != "unavailable") {
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return service->stats()->GetFarbledReads(page).webgl >= 1;
    }));
    EXPECT_EQ(1u, service->stats()->GetFarbledReads(page).webgl);
  }

  // A new document starts a new receipt, whether or not the frame is reused.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
  contents = browser()->tab_strip_model()->GetActiveWebContents();
  const content::GlobalRenderFrameHostToken next_page =
      contents->GetPrimaryMainFrame()->GetGlobalFrameToken();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return service->stats()->GetFarbledReads(next_page).total() == 0;
  }));
}

// "New identity" gives one site a fresh pattern, live, and what the browser
// says the site sees is exactly what the site's own scripts are told: the
// same generator on the same token, run in both processes.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, NewIdentityChangesWhatTheSiteSees) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  // localhost is a secure context, so deviceMemory is exposed too.
  const GURL site = embedded_test_server()->GetURL("localhost", "/farble.html");
  const GURL other = embedded_test_server()->GetURL("a.example", "/farble.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  const std::string scope = AdBlockService::IdentityScopeFor(contents);
  const auto seen_hardware = [&]() {
    return ParseProbe(content::EvalJs(contents, kHardwareProbe).ExtractString());
  };
  const auto canvas_url = [&]() -> std::string {
    const base::DictValue dict =
        ParseProbe(content::EvalJs(contents, kCanvasProbe).ExtractString());
    return *dict.FindString("url");
  };

  const SiteIdentity before = service->DescribeIdentity(site, scope);
  ASSERT_TRUE(before.farbled);
  const uint64_t other_before = service->GetFarblingToken(other, scope);
  base::DictValue seen = seen_hardware();
  EXPECT_EQ(static_cast<int>(before.reported_cores), *seen.FindInt("cores"))
      << "the panel describes what the renderer actually reports";
  EXPECT_EQ(static_cast<double>(before.reported_memory_gib),
            *seen.FindDouble("memory"));
  const std::string url_before = canvas_url();

  // What the panel's chip does.
  service->RotateIdentity(site, scope);
  contents->OnWebPreferencesChanged();

  const SiteIdentity after = service->DescribeIdentity(site, scope);
  EXPECT_NE(before.token, after.token);
  seen = seen_hardware();
  EXPECT_EQ(static_cast<int>(after.reported_cores), *seen.FindInt("cores"));
  EXPECT_EQ(static_cast<double>(after.reported_memory_gib),
            *seen.FindDouble("memory"));
  EXPECT_NE(url_before, canvas_url())
      << "the live page farbles with the new pattern at once";
  EXPECT_EQ(other_before, service->GetFarblingToken(other, scope))
      << "no other site moved";
}

// Two ways a page could have asked for the true pixels and been given them.
//
// A float readback was perturbed in its own storage, where the low bits of a
// 32-bit float are worth about a twenty-thousandth of an 8-bit step - so
// rounding recovered the exact true colour, and asking for `rgba-float32` was
// all it took to turn the protection off. And a WebGL read into a pixel-pack
// buffer landed in GPU memory the farbling never touched, so getBufferSubData
// handed back the real drawing buffer.
IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, WideAndBufferedReadbacksCannotEscape) {
  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  const GURL site = embedded_test_server()->GetURL("a.example", "/farble.html");
  const auto go = [&]() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), site));
    return browser()->tab_strip_model()->GetActiveWebContents();
  };

  // A float read and an ordinary read of the same pixels must agree, or the
  // two can simply be differenced to recover the truth.
  constexpr char kAgreement[] = R"((() => {
    const canvas = document.createElement('canvas');
    canvas.width = 16; canvas.height = 16;
    const context = canvas.getContext('2d', {willReadFrequently: true});
    context.fillStyle = '#3366cc';
    context.fillRect(0, 0, 16, 16);
    const eight = context.getImageData(0, 0, 16, 16).data;
    const wide = context.getImageData(0, 0, 16, 16,
                                      {pixelFormat: 'rgba-float32'}).data;
    for (let i = 0; i < eight.length; ++i) {
      if (Math.round(wide[i] * 255) !== eight[i]) { return 'differs at ' + i; }
    }
    return 'agree';
  })())";
  EXPECT_EQ("agree", content::EvalJs(go(), kAgreement).ExtractString())
      << "a float readback must not be recoverable to the true 8-bit colour";

  // A blank canvas must read back blank, in every format. Perturbing untouched
  // pixels both announces the protection and breaks "is this empty" checks.
  constexpr char kBlank[] = R"((() => {
    const canvas = document.createElement('canvas');
    canvas.width = 16; canvas.height = 16;
    const context = canvas.getContext('2d', {willReadFrequently: true});
    const wide = context.getImageData(0, 0, 16, 16,
                                      {pixelFormat: 'rgba-float32'}).data;
    for (let i = 0; i < wide.length; ++i) {
      if (wide[i] !== 0) { return 'non-zero at ' + i; }
    }
    return 'blank';
  })())";
  EXPECT_EQ("blank", content::EvalJs(go(), kBlank).ExtractString());

  // A read into a pixel-pack buffer is refused outright while protection is
  // on, because that destination cannot be perturbed.
  constexpr char kPackBuffer[] = R"((() => {
    const canvas = document.createElement('canvas');
    canvas.width = 8; canvas.height = 8;
    const gl = canvas.getContext('webgl2');
    if (!gl) { return 'unavailable'; }
    gl.clearColor(0.2, 0.4, 0.6, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, buffer);
    gl.bufferData(gl.PIXEL_PACK_BUFFER, 8 * 8 * 4, gl.STREAM_READ);
    while (gl.getError() !== gl.NO_ERROR) {}
    gl.readPixels(0, 0, 8, 8, gl.RGBA, gl.UNSIGNED_BYTE, 0);
    return String(gl.getError());
  })())";
  const std::string packed =
      content::EvalJs(go(), kPackBuffer).ExtractString();
  if (packed == "unavailable") {
    LOG(WARNING) << "WebGL2 is unavailable here; the pixel-pack path was not "
                    "exercised by this run";
  } else {
    EXPECT_EQ("1282", packed)
        << "a pixel-pack readback must be refused, not answered truthfully";
  }
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       AppliesOnlyBoundedProceduralCosmeticOperations) {
  ReplaceAdditionalRules(
      "news.example##.sponsored:has-text(Promoted)\n"
      "news.example##.remove-me:remove()\n");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.example", "/procedural.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(
      true,
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const matched = getComputedStyle("
          "    document.getElementById('procedural-match')).display === 'none';"
          "  const dynamic = getComputedStyle("
          "    document.getElementById('procedural-dynamic')).display === "
          "'none';"
          "  const removed = "
          "    document.getElementById('procedural-remove') === null;"
          "  matched && dynamic && removed"
          "    ? resolve(true) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
  EXPECT_NE("none", content::EvalJs(contents,
                                    "getComputedStyle(document.getElementById("
                                    "'procedural-unmatched')).display"));
  EXPECT_EQ("undefined",
            content::EvalJs(contents,
                            "typeof globalThis.__seoulProceduralFilterState"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       RevalidatesCosmeticRulesAfterHistoryRestore) {
  ReplaceRules("news.example##.domain-ad\n");
  const GURL cosmetic_url =
      embedded_test_server()->GetURL("news.example", "/cosmetic.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), cosmetic_url));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ(
      "none",
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const value = getComputedStyle("
          "    document.getElementById('domain-ad')).display;"
          "  value === 'none' ? resolve(value) : requestAnimationFrame(check);"
          " }; check();"
          "})"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("other.example", "/other.html")));
  ASSERT_TRUE(content::HistoryGoBack(contents));
  EXPECT_EQ(cosmetic_url, contents->GetLastCommittedURL());
  EXPECT_EQ(
      "none",
      content::EvalJs(
          contents,
          "new Promise(resolve => {"
          " const check = () => {"
          "  const value = getComputedStyle("
          "    document.getElementById('domain-ad')).display;"
          "  value === 'none' ? resolve(value) : requestAnimationFrame(check);"
          " }; check();"
          "})"));
}


// ---- `$csp` end-to-end -----------------------------------------------------
// Each case asserts renderer-observable behavior, never just a header string.

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, CspRuleBlocksInlineScript) {
  const GURL url = embedded_test_server()->GetURL("news.test", "/csp.html");

  // Baseline: with no matching rule the inline script runs.
  ASSERT_NO_FATAL_FAILURE(ReplaceRules(""));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, content::EvalJs(contents, "window.scriptRan === true"));

  // With the rule the very same script is refused by Blink's CSP machinery,
  // which proves the policy arrived before the document was processed.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  contents = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(false, content::EvalJs(contents, "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, CspExceptionSuppressesInjection) {
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"
                   "@@||news.test^$csp\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.test", "/csp.html")));
  EXPECT_EQ(true,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, ExistingSiteCspIsPreserved) {
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.test", "/csp_existing.html")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // Injected policy is enforced.
  EXPECT_EQ(false, content::EvalJs(contents, "window.scriptRan === true"));
  // The site's own `img-src 'none'` is still enforced, so it was neither
  // replaced nor relaxed by the injection.
  EXPECT_EQ(0, content::EvalJs(
                   contents,
                   "document.getElementById('img').naturalWidth"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, ReportOnlyCspIsNotPromoted) {
  // The site's report-only policy must stay report-only: with no matching
  // blocker rule the script still runs.
  ASSERT_NO_FATAL_FAILURE(ReplaceRules("||other.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.test", "/csp_report_only.html")));
  EXPECT_EQ(true,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, MultipleCspRulesCombine) {
  // One directive from each engine group; both must take effect.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"));
  ASSERT_NO_FATAL_FAILURE(
      ReplaceAdditionalRules("||news.test^$csp=img-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.test", "/csp_existing.html")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(false, content::EvalJs(contents, "window.scriptRan === true"));
  EXPECT_EQ(0, content::EvalJs(
                   contents,
                   "document.getElementById('img').naturalWidth"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, OffModeSuppressesCspInjection) {
  const GURL url = embedded_test_server()->GetURL("news.test", "/csp.html");
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"));

  AdBlockService* service =
      AdBlockServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  // Site settings are keyed by site, matching the other Off/Aggressive tests.
  service->SetSiteMode(embedded_test_server()->GetURL("news.test", "/"),
                       AdBlockMode::kOff);
  // Isolate settings from the CSP path: if this holds, any injection that
  // still happens is the response component ignoring the mode.
  ASSERT_EQ(AdBlockMode::kOff, service->GetSiteSettings(url).effective_mode);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  EXPECT_EQ(true,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, NonMatchingPageIsUnaffected) {
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||ads.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.test", "/csp.html")));
  EXPECT_EQ(true,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       CspAppliesToCrossOriginSubframeOnly) {
  // The rule names only the iframe's host, so the embedder must be untouched.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||frame.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("news.test", "/csp_parent.html")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  EXPECT_EQ(true, content::EvalJs(contents, "window.scriptRan === true"));

  content::RenderFrameHost* child = content::ChildFrameAt(contents, 0);
  ASSERT_TRUE(child);
  EXPECT_EQ(false, content::EvalJs(child, "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest, CspUsesFinalUrlAfterRedirect) {
  // Only the post-redirect host matches, so the final document is filtered.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||final.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("start.test", "/csp_redirect")));
  ASSERT_EQ(embedded_test_server()->GetURL("final.test", "/csp.html"),
            browser()->tab_strip_model()->GetActiveWebContents()
                ->GetLastCommittedURL());
  EXPECT_EQ(false,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       CspRuleForPreRedirectUrlDoesNotAffectFinalDocument) {
  // The inverse: a rule matching only the pre-redirect hop must not leak onto
  // the document that actually commits.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||start.test^$csp=script-src 'none'\n"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("start.test", "/csp_redirect")));
  ASSERT_EQ(embedded_test_server()->GetURL("final.test", "/csp.html"),
            browser()->tab_strip_model()->GetActiveWebContents()
                ->GetLastCommittedURL());
  EXPECT_EQ(true,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));
}

IN_PROC_BROWSER_TEST_F(AdBlockBrowserTest,
                       CspPolicyDoesNotLeakToNextDocument) {
  // Each navigation defers and resumes independently. After leaving a filtered
  // document, the next one must be evaluated on its own terms - a stale policy
  // from the previous response must not survive.
  ASSERT_NO_FATAL_FAILURE(
      ReplaceRules("||news.test^$csp=script-src 'none'\n"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("news.test", "/csp.html")));
  EXPECT_EQ(false,
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "window.scriptRan === true"));

  const GURL second = embedded_test_server()->GetURL("other.test", "/csp.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(second, contents->GetLastCommittedURL());
  EXPECT_EQ(true, content::EvalJs(contents, "window.scriptRan === true"));
}

}  // namespace
}  // namespace seoul::adblock
