// Project Seoul cosmetic filtering renderer tests.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "chrome/test/base/chrome_render_view_test.h"
#include "content/public/renderer/render_frame.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "seoul/browser/adblock/cosmetic_filter.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"

namespace seoul::renderer {
namespace {

class FakeCosmeticFilterHost : public adblock::mojom::CosmeticFilterHost {
 public:
  FakeCosmeticFilterHost() = default;
  ~FakeCosmeticFilterHost() override = default;

  void BindHandle(mojo::ScopedMessagePipeHandle handle) {
    receivers_.Add(this,
                   mojo::PendingReceiver<adblock::mojom::CosmeticFilterHost>(
                       std::move(handle)));
  }

  void set_enabled(bool enabled) { enabled_ = enabled; }
  void set_initial_selectors(std::vector<std::string> selectors) {
    initial_selectors_ = std::move(selectors);
  }
  void set_isolated_script(std::string script) {
    isolated_script_ = std::move(script);
  }
  void set_procedural_actions(std::vector<std::string> actions) {
    procedural_actions_ = std::move(actions);
  }

  int dynamic_query_count() const { return dynamic_query_count_; }

  // adblock::mojom::CosmeticFilterHost:
  void GetCosmeticResources(GetCosmeticResourcesCallback callback) override {
    auto resources = adblock::mojom::CosmeticResources::New();
    resources->enabled = enabled_;
    resources->default_rules = adblock::mojom::CosmeticSelectorSet::New();
    resources->default_rules->selectors = initial_selectors_;
    resources->default_rules->isolated_script = isolated_script_;
    resources->default_rules->procedural_actions = procedural_actions_;
    resources->default_rules->query_generics = true;
    resources->additional_rules = adblock::mojom::CosmeticSelectorSet::New();
    std::move(callback).Run(std::move(resources));
  }

  void GetDynamicCosmeticSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      GetDynamicCosmeticSelectorsCallback callback) override {
    ++dynamic_query_count_;
    auto selectors = adblock::mojom::DynamicCosmeticSelectors::New();
    if (std::ranges::find(classes, "generic-ad") != classes.end()) {
      selectors->default_selectors.push_back(".generic-ad");
    }
    if (std::ranges::find(ids, "generic-banner") != ids.end()) {
      selectors->default_selectors.push_back("#generic-banner");
    }
    std::move(callback).Run(std::move(selectors));
  }

 private:
  bool enabled_ = true;
  int dynamic_query_count_ = 0;
  std::vector<std::string> initial_selectors_;
  std::string isolated_script_;
  std::vector<std::string> procedural_actions_;
  mojo::ReceiverSet<adblock::mojom::CosmeticFilterHost> receivers_;
};

class CosmeticFilterAgentTest : public ChromeRenderViewTest {
 protected:
  void SetUp() override {
    ChromeRenderViewTest::SetUp();
    GetMainRenderFrame()->GetBrowserInterfaceBroker().SetBinderForTesting(
        adblock::mojom::CosmeticFilterHost::Name_,
        base::BindRepeating(&FakeCosmeticFilterHost::BindHandle,
                            base::Unretained(&host_)));
  }

  void TearDown() override {
    GetMainRenderFrame()->GetBrowserInterfaceBroker().SetBinderForTesting(
        adblock::mojom::CosmeticFilterHost::Name_, {});
    ChromeRenderViewTest::TearDown();
  }

  int EvaluateBoolean(std::u16string script) {
    int result = 0;
    EXPECT_TRUE(ExecuteJavaScriptAndReturnIntValue(script, &result));
    return result;
  }

  FakeCosmeticFilterHost host_;
};

TEST_F(CosmeticFilterAgentTest, AppliesInitialAndDynamicRulesAtUserOrigin) {
  host_.set_initial_selectors({".domain-ad"});
  LoadHTMLWithUrlOverride(
      "<div id='domain' class='domain-ad'></div>"
      "<div id='generic' class='generic-ad'></div>"
      "<div id='generic-banner'></div>",
      "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(
      1, EvaluateBoolean(u"getComputedStyle(document.getElementById('domain'))."
                         u"display === 'none' ? 1 : 0"));
  EXPECT_EQ(1, EvaluateBoolean(
                   u"getComputedStyle(document.getElementById('generic'))."
                   u"display === 'none' ? 1 : 0"));
  EXPECT_EQ(1,
            EvaluateBoolean(u"getComputedStyle(document.getElementById("
                            u"'generic-banner')).display === 'none' ? 1 : 0"));
  EXPECT_GE(host_.dynamic_query_count(), 1);
}

TEST_F(CosmeticFilterAgentTest, IsolatedWorldStateIsNotPageVisible) {
  LoadHTMLWithUrlOverride("<p>page</p>", "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1,
            EvaluateBoolean(
                u"typeof globalThis.__seoulCosmeticFilterState === 'undefined'"
                u" ? 1 : 0"));
}

TEST_F(CosmeticFilterAgentTest, RunsVettedScriptOnlyInIsolatedWorld) {
  host_.set_isolated_script(
      "function seoulRemoveElements(selector) {"
      "  for (const node of document.querySelectorAll(selector)) node.remove();"
      "}"
      "seoulRemoveElements('.scriptlet-ad');");
  LoadHTMLWithUrlOverride("<div class='scriptlet-ad'>ad</div>",
                          "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1,
            EvaluateBoolean(
                u"document.querySelector('.scriptlet-ad') === null ? 1 : 0"));
  EXPECT_EQ(1, EvaluateBoolean(
                   u"typeof globalThis.seoulRemoveElements === 'undefined'"
                   u" ? 1 : 0"));
}

TEST_F(CosmeticFilterAgentTest, AppliesBoundedProceduralRule) {
  host_.set_procedural_actions({
      R"({"selector":[{"type":"css-selector","arg":".sponsored"},{"type":"has-text","arg":"Promoted"}]})",
  });
  LoadHTMLWithUrlOverride(
      "<div id='matched' class='sponsored'>Promoted offer</div>"
      "<div id='unmatched' class='sponsored'>Article</div>",
      "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, EvaluateBoolean(
                   u"getComputedStyle(document.getElementById('matched'))."
                   u"display === 'none' ? 1 : 0"));
  EXPECT_EQ(1, EvaluateBoolean(
                   u"getComputedStyle(document.getElementById('unmatched'))."
                   u"display !== 'none' ? 1 : 0"));
  EXPECT_EQ(
      1, EvaluateBoolean(
             u"typeof globalThis.__seoulProceduralFilterState === 'undefined'"
             u" ? 1 : 0"));
}

TEST_F(CosmeticFilterAgentTest, RejectsSelectorDeclarationInjection) {
  host_.set_initial_selectors({".safe-ad", "body{opacity:0}"});
  LoadHTMLWithUrlOverride("<div class='safe-ad'></div>",
                          "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, EvaluateBoolean(
                   u"getComputedStyle(document.querySelector('.safe-ad'))."
                   u"display === 'none' ? 1 : 0"));
  EXPECT_EQ(1, EvaluateBoolean(
                   u"getComputedStyle(document.body).opacity === '1' ? 1 : 0"));
}

TEST_F(CosmeticFilterAgentTest, OffResponseInjectsNoStyles) {
  host_.set_enabled(false);
  host_.set_initial_selectors({".must-stay-visible"});
  LoadHTMLWithUrlOverride("<div class='must-stay-visible'></div>",
                          "https://news.example/article");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(
      1, EvaluateBoolean(u"getComputedStyle(document.querySelector("
                         u"'.must-stay-visible')).display !== 'none' ? 1 : 0"));
  EXPECT_EQ(0, host_.dynamic_query_count());
}

}  // namespace
}  // namespace seoul::renderer
