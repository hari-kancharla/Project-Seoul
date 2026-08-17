// Project Seoul native blocker browser-process integration tests.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
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
