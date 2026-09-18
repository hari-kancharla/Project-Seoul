// Project Seoul native browser shell V0.
//
// Real in-process browser tests for the Seoul shell integration. They run
// against a real Browser with the Seoul organization service attached to the
// regular profile and assert the load-bearing integration invariants: the
// profile-scoped services exist and are wired, the Chromium tab strip remains
// the owner of tabs (Seoul projects; it does not replace), and the model is
// reachable through the service. Wired into //chrome/test:browser_tests via
// the native-core integration patch.

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_views.h"
#include "chrome/browser/ui/views/omnibox/omnibox_result_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_row_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/tabs/shared/new_tab_button.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_view.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/content/content_platform_specific_tab_data.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "seoul/browser/lifecycle/new_tab_placeholder_provenance.h"
#include "seoul/browser/lifecycle/session_restore_metadata.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/projection/workspace_switcher.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/shell_service.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_shell_footer_view.h"
#include "seoul/browser/shell/views/seoul_shell_header_view.h"
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/actions/actions.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace seoul {
namespace {

struct PendingFaviconRequest {
  GURL page_url;
  favicon_base::FaviconImageCallback callback;
};

class ControllableFaviconLookup {
 public:
  void Lookup(const GURL& page_url,
              favicon_base::FaviconImageCallback callback) {
    requests.push_back({page_url, std::move(callback)});
  }

  std::vector<PendingFaviconRequest> requests;
};

favicon_base::FaviconImageResult SolidFavicon(SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(gfx::kFaviconSize, gfx::kFaviconSize);
  bitmap.eraseColor(color);
  favicon_base::FaviconImageResult result;
  result.image = gfx::Image::CreateFrom1xBitmap(bitmap);
  return result;
}

std::u16string ExpectedSeoulPlaceholder(Browser* browser) {
  const TemplateURL* const default_provider =
      TemplateURLServiceFactory::GetForProfile(browser->profile())
          ->GetDefaultSearchProvider();
  return default_provider
             ? l10n_util::GetStringFUTF16(
                   IDS_SEOUL_OMNIBOX_PLACEHOLDER_TEXT_WITH_ENGINE,
                   default_provider->short_name())
             : l10n_util::GetStringUTF16(IDS_SEOUL_OMNIBOX_PLACEHOLDER_TEXT);
}

void CollectVerticalTabViews(views::View* root,
                             std::vector<VerticalTabView*>* tabs) {
  if (auto* tab = views::AsViewClass<VerticalTabView>(root)) {
    tabs->push_back(tab);
  }
  for (views::View* child : root->children()) {
    CollectVerticalTabViews(child, tabs);
  }
}

// Lowercase letters, digits, and spaces are enough to type a real query, and
// keeping the mapping explicit means the tests send the key codes a keyboard
// sends rather than calling a text setter.
ui::KeyboardCode KeyCodeForCharacter(char character) {
  if (character >= 'a' && character <= 'z') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_A + (character - 'a'));
  }
  if (character >= '0' && character <= '9') {
    return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (character - '0'));
  }
  switch (character) {
    case ' ':
      return ui::VKEY_SPACE;
    case '.':
      return ui::VKEY_OEM_PERIOD;
    case '-':
      return ui::VKEY_OEM_MINUS;
    case '/':
      return ui::VKEY_OEM_2;
    case ':':
      return ui::VKEY_OEM_1;
    default:
      NOTREACHED() << "no key code mapped for '" << character << "'";
  }
}

void TypeWithRealKeys(ui::test::EventGenerator& generator,
                      std::string_view text) {
  for (const char character : text) {
    const bool shifted = character == ':';
    generator.PressAndReleaseKey(
        KeyCodeForCharacter(shifted ? character : std::tolower(character)),
        shifted ? ui::EF_SHIFT_DOWN : ui::EF_NONE);
  }
}

}  // namespace

class SeoulShellBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser() && browser()->window()) {
      browser()->window()->Hide();
      browser()->window()->ShowInactive();
    }
  }

  SeoulOrganizationService* service() {
    return SeoulOrganizationServiceFactory::GetForProfile(browser()->profile());
  }

  LiveWindowKey WindowKey() const {
    return LiveWindowKey::FromSessionId(browser()->session_id().id());
  }

  // A search provider this test process owns, so what Return produces is
  // decided by Chromium's configured default provider - never by a hardcoded
  // engine - and is identical on every machine that runs the suite.
  void UseControlledSearchProvider() {
    TemplateURLService* const model =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    ASSERT_TRUE(model);
    search_test_utils::WaitForTemplateURLServiceToLoad(model);
    ASSERT_TRUE(model->loaded());

    TemplateURLData data;
    data.SetShortName(u"SeoulTestSearch");
    data.SetKeyword(u"seoultest");
    data.SetURL("https://search.test/find?q={searchTerms}");
    model->SetUserSelectedDefaultSearchProvider(
        model->Add(std::make_unique<TemplateURL>(data)));

    // Prepopulated engines would otherwise turn up as suggestions and decide
    // which match Return opens.
    for (TemplateURL* const url : model->GetTemplateURLs()) {
      if (url->prepopulate_id() != 0) {
        model->Remove(url);
      }
    }
  }

  ui::test::EventGenerator MakeEventGenerator() {
    browser()->window()->Activate();
    gfx::NativeWindow event_window = browser()->window()->GetNativeWindow();
#if defined(USE_AURA)
    event_window = event_window->GetRootWindow();
#endif
    return ui::test::EventGenerator(event_window);
  }

  LiveTabKey TabKeyAt(int index) const {
    return TabStripBridge::KeyForTab(
        browser()->tab_strip_model()->GetTabAtIndex(index));
  }
};

// The profile-scoped Seoul runtime services are constructed and wired for a
// regular profile: the real Seoul runtime is linked into Chrome, not a dead
// library.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, ServicesWiredForRegularProfile) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  EXPECT_TRUE(svc->projection_service());
  EXPECT_TRUE(svc->shell_service());
  EXPECT_TRUE(svc->command_executor());
  EXPECT_TRUE(svc->lifecycle_coordinator());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       FreshWindowSettlesWithoutFailOpenBanner) {
  base::RunLoop().RunUntilIdle();
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ShellController* controller =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  EXPECT_NE(controller->snapshot().status, ShellStatus::kFailOpen);
  EXPECT_FALSE(controller->snapshot().show_status_banner);
  EXPECT_NE(controller->snapshot().status_message,
            "Showing all tabs while the layout recovers.");
}

// The current Space is named exactly once, in the footer pill. The rail used
// to carry a second name above the tabs; that indicator is deliberately no
// longer installed, and this test is the contract that it stays out.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, SpaceIsNamedOnceInTheFooter) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  // The old top-of-rail indicator class is deleted outright, so the only
  // thing left to hold is that the footer pill actually carries the name.
  SeoulShellFooterView* footer =
      svc->shell_service()->GetFooterForTesting(WindowKey());
  ASSERT_TRUE(footer);
  views::View* strip = footer->workspaces_control_for_testing();
  ASSERT_TRUE(strip);
  ASSERT_FALSE(strip->children().empty());
  auto* pill =
      views::AsViewClass<views::LabelButton>(strip->children().front());
  ASSERT_TRUE(pill);
  EXPECT_EQ(u"Default", pill->GetText())
      << "the footer pill is the single place the current Space is named";
}

// Pinned-section collapse survives the removal of the Space indicator that
// used to host its toggle. The strip API is the contract; the affordance gets
// a new home when pinned tabs get their next pass.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       PinnedCollapseWorksWithoutTheSpaceIndicator) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);
  VerticalTabStripView* const tab_strip =
      vertical_region->GetSeoulTabStripView();
  ASSERT_TRUE(tab_strip);

  tab_strip->SetSeoulPinnedTabsCollapsed(true);
  EXPECT_TRUE(tab_strip->seoul_pinned_tabs_collapsed_for_testing());
  tab_strip->SetSeoulPinnedTabsCollapsed(false);
  EXPECT_FALSE(tab_strip->seoul_pinned_tabs_collapsed_for_testing());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, NewTabUsesNeutralGlobeGlyph) {
  ASSERT_NE(nullptr, ui_test_utils::NavigateToURL(
                         browser(), GURL(chrome::kChromeUINewTabURL)));
  tabs::TabInterface* const tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(tab);
  TabUIHelper* const helper = TabUIHelper::From(tab);
  ASSERT_TRUE(helper);
  const ui::ImageModel favicon = helper->GetFavicon();
  ASSERT_TRUE(favicon.IsVectorIcon());
  EXPECT_EQ(&kGlobeIcon, favicon.GetVectorIcon().vector_icon());
  EXPECT_EQ(gfx::Size(gfx::kFaviconSize, gfx::kFaviconSize), favicon.Size());
}

// Seoul's defining shell is the default product surface. It must not disappear
// behind an upstream feature flag or a fresh-profile preference.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, VerticalShellIsOnByDefault) {
  EXPECT_TRUE(tabs::IsVerticalTabsFeatureEnabled());
  EXPECT_TRUE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kVerticalTabsEnabled));
  EXPECT_TRUE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kVerticalTabsEnabledFirstTime));

  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(controller);
  EXPECT_TRUE(controller->ShouldDisplayVerticalTabs());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, SingleToolbarUsesZenTabGeometry) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);

  browser_view->DeprecatedLayoutImmediately();
  std::vector<VerticalTabView*> tabs;
  CollectVerticalTabViews(vertical_region, &tabs);
  ASSERT_EQ(1u, tabs.size());
  VerticalTabView* const tab = tabs.front();

  EXPECT_EQ(40, GetLayoutConstant(LayoutConstant::kVerticalTabHeight));
  EXPECT_EQ(
      6, GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding));
  EXPECT_EQ(40, tab->height());
  EXPECT_EQ(gfx::Rect(2, 2, tab->width() - 4, 36),
            tab->GetBackgroundBoundsForTesting());

  const gfx::Rect region_bounds = vertical_region->GetBoundsInScreen();
  const gfx::Rect tab_bounds = tab->GetBoundsInScreen();
  EXPECT_EQ(6, tab_bounds.x() - region_bounds.x());
  EXPECT_EQ(6, region_bounds.right() - tab_bounds.right());
}

// Reapplying the already-active product mode must be harmless. This occurs
// during preference restore and used to reset an uninitialized horizontal tab
// strip when Seoul started directly in vertical mode.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ReapplyingVerticalModeIsIdempotent) {
  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->ShouldDisplayVerticalTabs());

  controller->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(controller->ShouldDisplayVerticalTabs());
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_EQ(static_cast<views::View*>(
                browser_view->vertical_tab_strip_region_view_for_testing()),
            browser_view->toolbar()->parent());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       OmniboxIsIntegratedIntoVerticalShell) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);
  ASSERT_TRUE(browser_view->toolbar());
  EXPECT_EQ(static_cast<views::View*>(vertical_region),
            browser_view->toolbar()->parent());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SingleToolbarKeepsDockedAddressRow) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);

  browser_view->DeprecatedLayoutImmediately();
  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());
  EXPECT_TRUE(location_bar->GetVisible());
  EXPECT_EQ(38, location_bar->height());
  EXPECT_EQ(42, location_bar->y());

  browser_view->SetSeoulOmniboxFloating(true);
  ASSERT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            location_bar->parent());
  browser_view->SetSeoulOmniboxFloating(false);
  browser_view->DeprecatedLayoutImmediately();

  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());
  EXPECT_TRUE(location_bar->GetVisible());
  EXPECT_EQ(38, location_bar->height());
  EXPECT_EQ(42, location_bar->y());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SingleToolbarControlsUseCenteredZenPressMotion) {
  auto render_mode_lock = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  browser_view->DeprecatedLayoutImmediately();

  const std::array<views::Button*, 4> controls = {
      browser_view->toolbar_button_provider()->GetBackButton(),
      toolbar->forward_button(), toolbar->reload_button(),
      toolbar->seoul_compact_button_for_testing()};
  for (size_t i = 0; i < controls.size(); ++i) {
    views::Button* const control = controls[i];
    ASSERT_TRUE(control);
    ASSERT_TRUE(control->GetVisible());
    ASSERT_FALSE(control->layer());
    const bool was_enabled = control->GetEnabled();
    control->SetEnabled(true);

    control->SetState(views::Button::STATE_PRESSED);
    ASSERT_TRUE(control->layer());
    const gfx::Transform target = control->layer()->GetTargetTransform();
    const gfx::Vector2dF scale = target.To2dScale();
    EXPECT_NEAR(0.95f, scale.x(), 0.001f);
    EXPECT_NEAR(0.95f, scale.y(), 0.001f);
    const gfx::PointF center(control->width() / 2.0f, control->height() / 2.0f);
    const gfx::PointF mapped_center = target.MapPoint(center);
    EXPECT_NEAR(center.x(), mapped_center.x(), 0.001f);
    EXPECT_NEAR(center.y(), mapped_center.y(), 0.001f);

    // Hover models a pointer release over the control; normal models a
    // cancelled press after the pointer or capture leaves it.
    control->SetState(i % 2 == 0 ? views::Button::STATE_HOVERED
                                 : views::Button::STATE_NORMAL);
    ASSERT_TRUE(control->layer());
    EXPECT_TRUE(control->layer()->GetTargetTransform().IsIdentity());
    EXPECT_TRUE(
        base::test::RunUntil([&]() { return control->layer() == nullptr; }));
    control->SetState(views::Button::STATE_NORMAL);
    control->SetEnabled(was_enabled);
  }

  // Leaving Single Toolbar must synchronously restore the transform, release
  // the owned layer, and unsubscribe from the now-hidden Seoul controls.
  views::Button* const compact = toolbar->seoul_compact_button_for_testing();
  compact->SetEnabled(true);
  compact->SetState(views::Button::STATE_PRESSED);
  ASSERT_TRUE(compact->layer());
  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kMultiple);
  EXPECT_FALSE(toolbar->is_seoul_sidebar_presentation());
  EXPECT_FALSE(compact->layer());

  compact->SetState(views::Button::STATE_NORMAL);
  compact->SetState(views::Button::STATE_PRESSED);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(compact->layer());
  compact->SetState(views::Button::STATE_NORMAL);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SingleToolbarPressMotionSnapsWhenReduced) {
  auto render_mode_lock = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  browser_view->DeprecatedLayoutImmediately();

  const std::array<views::Button*, 4> controls = {
      browser_view->toolbar_button_provider()->GetBackButton(),
      toolbar->forward_button(), toolbar->reload_button(),
      toolbar->seoul_compact_button_for_testing()};
  for (views::Button* control : controls) {
    ASSERT_TRUE(control);
    ASSERT_FALSE(control->layer());
    const bool was_enabled = control->GetEnabled();
    control->SetEnabled(true);

    control->SetState(views::Button::STATE_PRESSED);
    ASSERT_TRUE(control->layer());
    const gfx::Vector2dF scale = control->layer()->transform().To2dScale();
    EXPECT_NEAR(0.95f, scale.x(), 0.001f);
    EXPECT_NEAR(0.95f, scale.y(), 0.001f);

    control->SetState(views::Button::STATE_NORMAL);
    EXPECT_FALSE(control->layer());
    control->SetEnabled(was_enabled);
  }
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       IntegratedRailDoesNotPaintChromiumSeamCorners) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ASSERT_TRUE(browser_view->IsSeoulToolbarIntegrated());
  views::View* const top_corner =
      browser_view->vertical_tab_strip_top_corner_for_testing();
  views::View* const bottom_corner =
      browser_view->vertical_tab_strip_bottom_corner_for_testing();
  ASSERT_TRUE(top_corner);
  ASSERT_TRUE(bottom_corner);

  browser_view->DeprecatedLayoutImmediately();
  EXPECT_FALSE(top_corner->GetVisible());
  EXPECT_FALSE(bottom_corner->GetVisible());
  EXPECT_TRUE(top_corner->bounds().IsEmpty());
  EXPECT_TRUE(bottom_corner->bounds().IsEmpty());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SingleToolbarOwnsFocusedStartupPlaceholder) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);

  location_bar->FocusLocation(/*is_user_initiated=*/false,
                              /*clear_focus_if_failed=*/false);
  omnibox->InstallPlaceholderText();
  browser_view->DeprecatedLayoutImmediately();

  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());
  EXPECT_TRUE(location_bar->seoul_sidebar_mode());
  EXPECT_FALSE(location_bar->seoul_floating_mode());
  EXPECT_EQ(ExpectedSeoulPlaceholder(browser()), omnibox->GetPlaceholderText());

  location_bar->SetSeoulSidebarMode(false);
  location_bar->SetSeoulSidebarMode(true);
  EXPECT_EQ(ExpectedSeoulPlaceholder(browser()), omnibox->GetPlaceholderText());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SingleToolbarUsesZenLeadingSearchTreatment) {
  // Exercise docked page controls, after leaving the startup search surface.
  ASSERT_TRUE(embedded_test_server()->Start());
  const auto page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  ASSERT_TRUE(location_bar->location_icon_view());
  ASSERT_TRUE(location_bar->seoul_floating_search_icon_for_testing());

  // Enter editing on an actual page. The separate startup test covers the
  // floating placeholder; its location bar is not docked in the toolbar.
  ASSERT_EQ(browser_view->toolbar(), location_bar->parent());
  location_bar->Update(browser()->tab_strip_model()->GetActiveWebContents());
  browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true);
  location_bar->GetOmniboxView()->SetUserText(u"seoul");
  browser_view->DeprecatedLayoutImmediately();

  EXPECT_TRUE(location_bar->IsEditingOrEmpty());
  EXPECT_TRUE(
      location_bar->seoul_floating_search_icon_for_testing()->GetVisible());
  EXPECT_FALSE(location_bar->location_icon_view()->GetVisible());

  ASSERT_NE(nullptr, ui_test_utils::NavigateToURL(browser(), page_url));
  location_bar->Revert();
  // Navigation preserves an active address edit. Leave editing through the
  // normal blur path before asserting the docked, steady-state decorations.
  browser_view->GetFocusManager()->ClearFocus();
  browser_view->FocusWebContentsPane();
  location_bar->Update(browser()->tab_strip_model()->GetActiveWebContents());
  browser_view->DeprecatedLayoutImmediately();
  SCOPED_TRACE(testing::Message()
               << "floating=" << location_bar->seoul_floating_mode()
               << " sidebar=" << location_bar->seoul_sidebar_mode()
               << " focused=" << location_bar->omnibox_view()->HasFocus()
               << " drawn=" << location_bar->IsDrawn() << " docked="
               << (location_bar->parent() == browser_view->toolbar())
               << " bounds=" << location_bar->bounds().ToString());

  EXPECT_FALSE(location_bar->IsEditingOrEmpty());
  EXPECT_FALSE(
      location_bar->seoul_floating_search_icon_for_testing()->GetVisible());
  EXPECT_FALSE(location_bar->location_icon_view()->GetVisible());

  // Zen temporarily restores the page identity on implicit hover, then
  // collapses it again when the pointer leaves.
  //
  // ZERO_DURATION alone does not make this synchronous: SlideAnimation only
  // short-circuits when its *configured* duration is zero, and the hover
  // animation's is not - the scale factor is applied afterwards, via
  // GetDuration(). So the animation still starts and still needs a tick from
  // its container, which RunUntilIdle does not deliver. Wait for the endpoint
  // instead of assuming it has already been reached.
  gfx::ScopedAnimationDurationScaleMode disable_animation(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  ASSERT_TRUE(location_bar->location_icon_view()->layer());
  location_bar->OnOmniboxHovered(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->DeprecatedLayoutImmediately();
    return location_bar->location_icon_view()->layer()->opacity() == 1.0f;
  })) << "hover must fade the page identity in to full opacity";
  EXPECT_TRUE(location_bar->location_icon_view()->GetVisible());

  location_bar->OnOmniboxHovered(false);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->DeprecatedLayoutImmediately();
    return location_bar->location_icon_view()->layer()->opacity() == 0.0f;
  })) << "leaving must fade the page identity back out";
  EXPECT_FALSE(location_bar->location_icon_view()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       FirstEditKeepsTextAndEmbedsAutocompleteResults) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  OmniboxPopupView* const popup_interface =
      location_bar->GetOmniboxPopupViewForTesting();
  auto* const popup = static_cast<OmniboxPopupViewViews*>(popup_interface);
  ASSERT_TRUE(popup);

  omnibox->SetFocus(/*is_user_initiated=*/false);
  omnibox->SetUserText(u"example.com", /*update_popup=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] { return popup_interface->IsOpen(); }));
  browser_view->DeprecatedLayoutImmediately();

  EXPECT_EQ(u"example.com",
            location_bar->GetOmniboxController()->edit_model()->user_text());
  EXPECT_TRUE(browser_view->IsHostingSeoulOmniboxPopup(popup));
  EXPECT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            location_bar->parent());
  EXPECT_GT(browser_view->seoul_omnibox_surface_for_testing()->height(), 62);
  EXPECT_EQ(browser_view->seoul_omnibox_surface_for_testing()->size(),
            browser_view->seoul_omnibox_backdrop_for_testing()->size());

  location_bar->GetOmniboxController()->StopAutocomplete(
      /*clear_result=*/true);
  popup->UpdatePopupAppearance();
  EXPECT_FALSE(popup_interface->IsOpen());
  EXPECT_FALSE(browser_view->IsHostingSeoulOmniboxPopup(popup));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       EmbeddedAutocompleteClickStaysInsideSurface) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  OmniboxPopupView* const popup_interface =
      location_bar->GetOmniboxPopupViewForTesting();
  auto* const popup = static_cast<OmniboxPopupViewViews*>(popup_interface);
  ASSERT_TRUE(popup);

  omnibox->SetFocus(/*is_user_initiated=*/false);
  omnibox->SetUserText(u"chrome://version/", /*update_popup=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] { return popup_interface->IsOpen(); }));
  browser_view->DeprecatedLayoutImmediately();
  ASSERT_FALSE(popup->children().empty());
  auto* const first_row =
      views::AsViewClass<OmniboxRowView>(popup->children().front());
  ASSERT_TRUE(first_row);
  OmniboxResultView* const first_result = first_row->result_view();
  ASSERT_TRUE(first_result);
  const GURL destination = location_bar->GetOmniboxController()
                               ->autocomplete_controller()
                               ->result()
                               .match_at(first_row->line())
                               .destination_url;
  ASSERT_TRUE(destination.is_valid());

  ui::MouseEvent press(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::Event::DispatcherApi(&press).set_target(first_result);
  EXPECT_FALSE(location_bar->ShouldCloseOmniboxPopup(&press));

  gfx::NativeWindow event_window = browser()->window()->GetNativeWindow();
#if defined(USE_AURA)
  event_window = event_window->GetRootWindow();
#endif
  ui::test::EventGenerator generator(event_window);
  generator.MoveMouseTo(first_result->GetBoundsInScreen().CenterPoint());
  generator.PressLeftButton();
  EXPECT_TRUE(popup_interface->IsOpen());
  EXPECT_TRUE(browser_view->IsHostingSeoulOmniboxPopup(popup));
  generator.ReleaseLeftButton();

  content::WebContents* const contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(destination, contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       EmbeddedAutocompleteOverflowScrolls) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  OmniboxPopupView* const popup_interface =
      location_bar->GetOmniboxPopupViewForTesting();
  auto* const popup = static_cast<OmniboxPopupViewViews*>(popup_interface);
  ASSERT_TRUE(popup);

  omnibox->SetFocus(/*is_user_initiated=*/false);
  omnibox->SetUserText(u"overflow", /*update_popup=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] { return popup_interface->IsOpen(); }));

  OmniboxController* const controller = location_bar->GetOmniboxController();
  ASSERT_TRUE(controller);
  AutocompleteController* const autocomplete =
      controller->autocomplete_controller();
  ASSERT_TRUE(autocomplete);
  ASSERT_FALSE(autocomplete->result().empty());
  AutocompleteProvider* const provider =
      autocomplete->result().match_at(0).provider;
  ASSERT_TRUE(provider);
  controller->StopAutocomplete(/*clear_result=*/false);

  const size_t initial_result_count = autocomplete->result().size();
  constexpr size_t kInjectedResultCount = 12;
  for (size_t i = 0; i < kInjectedResultCount; ++i) {
    const std::string suffix = base::NumberToString(i);
    AutocompleteMatch match(provider, 100 - static_cast<int>(i),
                            /*deletable=*/false,
                            AutocompleteMatchType::HISTORY_URL);
    match.contents = u"overflow.example/" + base::NumberToString16(i);
    match.contents_class = {{0, AutocompleteMatch::ACMatchClassification::URL}};
    match.fill_into_edit = match.contents;
    match.destination_url = GURL("https://overflow.example/" + suffix);
    match.transition = ui::PAGE_TRANSITION_TYPED;
    autocomplete->InjectAdHocMatch(std::move(match));
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return autocomplete->result().size() ==
           initial_result_count + kInjectedResultCount;
  }));
  popup->UpdatePopupAppearance();
  browser_view->DeprecatedLayoutImmediately();

  views::ScrollView* const scroll_view =
      browser_view->seoul_omnibox_popup_scroll_view_for_testing();
  ASSERT_TRUE(scroll_view);
  ASSERT_TRUE(scroll_view->contents());
  EXPECT_EQ(252, scroll_view->height());
  EXPECT_GT(scroll_view->contents()->height(), scroll_view->height());
  EXPECT_TRUE(scroll_view->IsVerticalContentOverflowing());
  EXPECT_EQ(scroll_view->contents()->height() -
                scroll_view->GetVisibleRect().height(),
            scroll_view->vertical_scroll_bar()->GetMaxPosition());
  EXPECT_FLOAT_EQ(0.0f, scroll_view->CurrentOffset().y());

  const size_t last_line = autocomplete->result().size() - 1;
  OmniboxRowView* last_row = nullptr;
  for (views::View* child : popup->children()) {
    auto* const row = views::AsViewClass<OmniboxRowView>(child);
    if (row && row->line() == last_line) {
      last_row = row;
      break;
    }
  }
  ASSERT_TRUE(last_row);

  browser()->window()->Activate();
  gfx::NativeWindow event_window = browser()->window()->GetNativeWindow();
#if defined(USE_AURA)
  event_window = event_window->GetRootWindow();
#endif
  ui::test::EventGenerator generator(event_window);
  generator.PressAndReleaseKey(ui::VKEY_NEXT);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return popup->GetSelectedIndex() == last_line; }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return scroll_view->CurrentOffset().y() > 0.0f; }));
  const gfx::Rect last_row_rect = views::View::ConvertRectToTarget(
      last_row, scroll_view->contents(), last_row->GetLocalBounds());
  EXPECT_TRUE(scroll_view->GetVisibleRect().Contains(last_row_rect));

  controller->StopAutocomplete(/*clear_result=*/true);
  popup->UpdatePopupAppearance();
}

// Scrolling the Space strip moves between Spaces, the way Zen and Arc do it.
//
// Covers the three things that are easy to get wrong: a wheel notch moves
// exactly one Space, the strip wraps rather than stopping at the ends, and a
// trackpad's stream of small deltas produces one switch per flick instead of
// racing through every Space in the window.
// The claim containers make, tested as a user would observe it: a cookie
// written in an isolated Space is not visible from another Space.
//
// This is the test that decides whether the feature exists. Everything else -
// the naming rule, the flag, the resolver - can be individually correct while
// data still crosses, and data crossing is worse than having no containers at
// all, because the user was told they were separate.
// The claim containers make, tested as a user would observe it: a cookie
// written in an isolated Space is invisible from every other Space, in both
// directions. Negative-controlled: with the isolation call removed this fails
// with "a cookie written in an isolated Space leaked into another Space", so a
// silent regression in the partition wiring cannot pass.
//
// History worth keeping: this test spent a day reporting the feature broken
// when the feature was fine. Two separate causes, both in the harness - the
// browser suite was built without rebuilding the component libraries it links
// (fixed in test.sh), and the Space switches used RunUntilIdle() where they
// had to wait for the model to actually report the switch (fixed below with
// RunUntil). If it goes red again, check those two things before touching the
// product.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       IsolatedSpaceDoesNotShareCookies) {
  ASSERT_TRUE(embedded_test_server()->Start());
  // The loopback host directly, so this needs no resolver rule - rules have to
  // be installed before the test body runs, and the host is irrelevant here
  // anyway: the point is the same site seen from two Spaces.
  const GURL url = embedded_test_server()->GetURL("/empty.html");

  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  auto isolated = svc->model().CreateWorkspace("Isolated");
  ASSERT_TRUE(isolated.has_value());
  ASSERT_TRUE(
      svc->model().SetWorkspaceIsolated(isolated.value(), true).has_value());
  // Verify the setup took, rather than trusting the mutation's return value.
  // A test whose precondition silently failed reports the feature broken when
  // the feature was never switched on.
  {
    const WorkspaceRecord* const check =
        svc->model().FindWorkspace(isolated.value());
    ASSERT_TRUE(check);
    ASSERT_TRUE(check->isolated) << "the Space did not become isolated";
  }

  const std::string window = WindowKey().value();
  const WorkspaceId ordinary = svc->model().ActiveWorkspaceForWindow(window);
  ASSERT_TRUE(ordinary.is_valid());
  ASSERT_NE(ordinary, isolated.value());

  // A new foreground tab per call, because the partition is chosen when the tab
  // is created: reusing one tab across a Space switch would prove nothing.
  auto open_tab_and_run = [&](const std::string& script) {
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
    content::WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    return content::EvalJs(contents, script);
  };

  // Switching Space and then opening a tab is a two-step dance, and the second
  // step must not start until the first has landed. RunUntilIdle() is not that
  // guarantee - it drains the queue once - so under a different scheduler the
  // tab was being created while the old Space was still active and landed in
  // the wrong partition. Wait for the model to actually report the switch.
  auto switch_to = [&](const WorkspaceId& target) {
    ASSERT_TRUE(
        svc->model().SetActiveWorkspaceForWindow(window, target).has_value());
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return svc->model().ActiveWorkspaceForWindow(window) == target;
    })) << "the Space switch never took effect";
  };

  // Write a cookie while the isolated Space is active.
  switch_to(isolated.value());
  ASSERT_EQ("set",
            open_tab_and_run("document.cookie = 'seoul=isolated; path=/';"
                             "'set'"));
  EXPECT_EQ("seoul=isolated", open_tab_and_run("document.cookie"))
      << "the isolated Space must see its own cookie";

  // The same site, from the ordinary Space, must not see it.
  switch_to(ordinary);
  EXPECT_EQ("", open_tab_and_run("document.cookie"))
      << "a cookie written in an isolated Space leaked into another Space";

  // And the reverse: what the ordinary Space writes stays out of the container.
  ASSERT_EQ("set",
            open_tab_and_run("document.cookie = 'seoul=ordinary; path=/';"
                             "'set'"));
  switch_to(isolated.value());
  EXPECT_EQ("seoul=isolated", open_tab_and_run("document.cookie"))
      << "the container must still hold only its own cookie";
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ContainerRestoreKeepsCookiesAndSessionStorage) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("/empty.html");
  auto& model = service()->model();
  const auto ordinary = model.default_workspace();
  const auto work = model.CreateWorkspace("Work", true).value();
  ASSERT_TRUE(
      model.SetActiveWorkspaceForWindow(WindowKey().value(), work).has_value());
  chrome::NewTab(browser());
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));
  EXPECT_EQ(work, ContainerWorkspaceForTab(
                      browser()->tab_strip_model()->GetActiveWebContents()));
  EXPECT_NE("newtab", browser()->tab_strip_model()
                          ->GetActiveWebContents()
                          ->GetPrimaryMainFrame()
                          ->GetLastCommittedURL()
                          .host())
      << "The virtual New Tab URL must resolve to its real WebUI";
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* source = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(work, ContainerWorkspaceForTab(source));
  ASSERT_TRUE(content::ExecJs(
      source,
      "document.cookie='account=work; path=/'; localStorage.account='work'; "
      "sessionStorage.draft='saved';"));
  std::map<std::string, std::string> metadata;
  PopulateSeoulSessionMetadata(source, &metadata);
  EXPECT_EQ(work.value(), metadata[kSeoulContainerSessionKey]);
  const std::vector<sessions::SerializedNavigationEntry> entries = {
      sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
          0, source->GetController().GetLastCommittedEntry())};
  sessions::ContentPlatformSpecificTabData platform_data(source);
  browser()->tab_strip_model()->CloseWebContentsAt(
      browser()->tab_strip_model()->active_index(), TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(model.DeleteWorkspace(work).has_value());
  ASSERT_TRUE(model.SetActiveWorkspaceForWindow(WindowKey().value(), ordinary)
                  .has_value());
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_TRUE(content::ExecJs(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "document.cookie='account=shared; path=/'; "
      "localStorage.account='shared'; sessionStorage.draft='other';"));
  auto* restored = chrome::AddRestoredTab(
      browser(), entries, browser()->tab_strip_model()->count(), 0,
      std::string(), std::nullopt, true, false, base::TimeTicks(), base::Time(),
      platform_data.session_storage_namespace(), {}, metadata, false,
      std::nullopt);
  ASSERT_TRUE(restored);
  ASSERT_TRUE(content::WaitForLoadStop(restored));
  EXPECT_EQ(work, ContainerWorkspaceForTab(restored));
  ASSERT_TRUE(model.FindWorkspace(work));
  EXPECT_TRUE(model.FindWorkspace(work)->isolated);
  EXPECT_EQ("Recovered Container", model.FindWorkspace(work)->name);
  EXPECT_EQ("account=work", content::EvalJs(restored, "document.cookie"));
  EXPECT_EQ("work", content::EvalJs(restored, "localStorage.account"));
  EXPECT_EQ("saved", content::EvalJs(restored, "sessionStorage.draft"));
  const auto membership = model.FindMembershipIdByTabKey(
      LiveTabKey::FromSessionId(
          sessions::SessionTabHelper::IdForTab(restored).id())
          .value());
  ASSERT_TRUE(membership.is_valid());
  EXPECT_EQ(work, model.FindMembership(membership)->workspace_id);
}

// The Space strip's shape contract: the current Space is a wide labelled tile,
// every other Space is a small square tile, and switching moves the pill.
// Pinned as a test because this is the strip's design - if a refactor collapses
// the two shapes into one, the strip stops saying where you are.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SpaceStripShapesFollowActivation) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  auto second = svc->model().CreateWorkspace("Play");
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(svc->model().SetWorkspaceIcon(second.value(), "🎮").has_value());
  base::RunLoop().RunUntilIdle();

  SeoulShellFooterView* footer =
      svc->shell_service()->GetFooterForTesting(WindowKey());
  ASSERT_TRUE(footer);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  browser_view->GetWidget()->LayoutRootViewIfNecessary();

  views::View* strip = footer->workspaces_control_for_testing();
  ASSERT_TRUE(strip);
  ASSERT_EQ(2u, strip->children().size());

  auto* first_button =
      views::AsViewClass<views::LabelButton>(strip->children()[0]);
  auto* second_button =
      views::AsViewClass<views::LabelButton>(strip->children()[1]);
  ASSERT_TRUE(first_button);
  ASSERT_TRUE(second_button);

  // The active Space (Default, first) is the pill: wider than tall, carrying
  // its name. The other is a square tile carrying its emoji.
  EXPECT_GT(first_button->width(), first_button->height());
  EXPECT_NE(first_button->GetText().find(u"Default"), std::u16string::npos);
  EXPECT_EQ(second_button->width(), second_button->height());
  EXPECT_EQ(u"🎮", second_button->GetText());

  // Switch, and the shapes swap.
  ShellController* controller =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->SwitchWorkspace(second.value()).has_value());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    return second_button->width() > second_button->height();
  })) << "the pill must move to the newly current Space";
  EXPECT_EQ(first_button->width(), first_button->height())
      << "the previous Space must shrink back to a square tile";
  EXPECT_NE(second_button->GetText().find(u"Play"), std::u16string::npos)
      << "the pill carries the name, not only the emoji";
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, ScrollingTheSpaceStripSwitches) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->model().CreateWorkspace("Second").has_value());
  ASSERT_TRUE(svc->model().CreateWorkspace("Third").has_value());
  base::RunLoop().RunUntilIdle();

  SeoulShellFooterView* footer =
      svc->shell_service()->GetFooterForTesting(WindowKey());
  ASSERT_TRUE(footer);
  views::View* strip = footer->workspaces_control_for_testing();
  ASSERT_TRUE(strip);

  ShellController* controller =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  ASSERT_GE(controller->snapshot().spaces.size(), 3u);

  auto active_index = [&]() -> int {
    const auto& spaces = controller->snapshot().spaces;
    for (size_t i = 0; i < spaces.size(); ++i) {
      if (spaces[i].is_active) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };
  const int count = static_cast<int>(controller->snapshot().spaces.size());
  const int start = active_index();
  ASSERT_GE(start, 0);

  auto wheel = [&](int y_offset) {
    ui::MouseWheelEvent event(gfx::Vector2d(0, y_offset), gfx::Point(1, 1),
                              gfx::Point(1, 1), base::TimeTicks::Now(),
                              ui::EF_NONE, ui::EF_NONE);
    strip->OnMouseWheel(event);
    base::RunLoop().RunUntilIdle();
  };

  // One notch down, one Space forward.
  wheel(-120);
  EXPECT_EQ((start + 1) % count, active_index());

  // And back.
  wheel(120);
  EXPECT_EQ(start, active_index());

  // Wraps rather than stopping: scrolling back past the first lands on the
  // last.
  wheel(120);
  EXPECT_EQ((start - 1 + count) % count, active_index())
      << "the strip should wrap, not stop at the end";

  // A trackpad flick is many small deltas. Below the threshold nothing moves.
  const int before_flick = active_index();
  for (int i = 0; i < 3; ++i) {
    ui::ScrollEvent small(ui::EventType::kScroll, gfx::Point(1, 1),
                          base::TimeTicks::Now(), ui::EF_NONE,
                          /*x_offset=*/0, /*y_offset=*/-5,
                          /*x_offset_ordinal=*/0, /*y_offset_ordinal=*/-5,
                          /*finger_count=*/2);
    strip->OnScrollEvent(&small);
  }
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(before_flick, active_index())
      << "incidental movement must not change Space";

  // Crossing the threshold moves exactly one, not one per delta.
  for (int i = 0; i < 8; ++i) {
    ui::ScrollEvent step(ui::EventType::kScroll, gfx::Point(1, 1),
                         base::TimeTicks::Now(), ui::EF_NONE,
                         /*x_offset=*/0, /*y_offset=*/-10,
                         /*x_offset_ordinal=*/0, /*y_offset_ordinal=*/-10,
                         /*finger_count=*/2);
    strip->OnScrollEvent(&step);
  }
  base::RunLoop().RunUntilIdle();
  const int moved = ((active_index() - before_flick) % count + count) % count;
  EXPECT_GE(moved, 1);
  EXPECT_LE(moved, 2) << "a single flick must not race through every Space";
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       FooterKeepsDailyControlsVisibleAndOrdered) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);

  SeoulShellFooterView* footer = nullptr;
  for (views::View* child : vertical_region->children()) {
    if (auto* candidate = views::AsViewClass<SeoulShellFooterView>(child)) {
      footer = candidate;
      break;
    }
  }
  ASSERT_TRUE(footer);

  footer->SetPresentationCollapsed(false);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  views::View* controls = footer->controls_row_for_testing();
  views::LabelButton* downloads = footer->downloads_button_for_testing();
  views::LabelButton* assistant = footer->assistant_button_for_testing();
  views::View* workspaces = footer->workspaces_control_for_testing();
  views::LabelButton* create_new = footer->create_new_button_for_testing();
  ASSERT_TRUE(controls);
  ASSERT_TRUE(downloads);
  ASSERT_TRUE(workspaces);
  ASSERT_TRUE(create_new);
  ASSERT_TRUE(assistant);

  // The assistant is discoverable beside the daily browser controls.
  const auto& children = controls->children();
  ASSERT_EQ(3u, children.size());
  EXPECT_EQ(downloads, children[0]);
  EXPECT_EQ(workspaces, children[1]);
  EXPECT_EQ(create_new, children[2]);
  EXPECT_TRUE(assistant->GetVisible());
  EXPECT_EQ(u"Ask Seoul", assistant->GetAccessibleName());
  EXPECT_LE(assistant->bounds().bottom(), controls->bounds().y());
  EXPECT_TRUE(downloads->GetVisible());
  EXPECT_EQ(u"Downloads", downloads->GetAccessibleName());
  EXPECT_EQ(downloads->GetPreferredSize().width(),
            create_new->GetPreferredSize().width());
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_EQ(u"Spaces", workspaces->GetAccessibleName());
  EXPECT_EQ(u"Create New", create_new->GetAccessibleName());

  const std::optional<ui::ImageModel>& create_new_icon =
      create_new->GetImageModel(views::Button::STATE_NORMAL);
  ASSERT_TRUE(create_new_icon);
  ASSERT_TRUE(create_new_icon->IsVectorIcon());
  EXPECT_EQ(&kSeoulPlusIcon, create_new_icon->GetVectorIcon().vector_icon());

  const std::optional<ui::ImageModel>& downloads_icon =
      downloads->GetImageModel(views::Button::STATE_NORMAL);
  ASSERT_TRUE(downloads_icon);
  ASSERT_TRUE(downloads_icon->IsVectorIcon());
  EXPECT_EQ(&kSeoulDownloadIcon, downloads_icon->GetVectorIcon().vector_icon());

  ASSERT_EQ(1u, workspaces->children().size());
  auto* workspace_button =
      views::AsViewClass<views::LabelButton>(workspaces->children().front());
  ASSERT_TRUE(workspace_button);
  const std::optional<ui::ImageModel>& workspace_icon =
      workspace_button->GetImageModel(views::Button::STATE_NORMAL);
  EXPECT_TRUE(!workspace_icon || workspace_icon->IsEmpty());
  EXPECT_TRUE(footer->first_space_uses_empty_icon_dot_for_testing());

  // The assistant has its own row; the Space strip keeps its full measure.
  EXPECT_LE(downloads->bounds().right(), workspaces->bounds().x());
  EXPECT_LE(workspaces->bounds().right(), create_new->bounds().x());
  EXPECT_LE(create_new->bounds().right(), controls->width());

  footer->SetPresentationCollapsed(true);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_LT(workspaces->bounds().CenterPoint().y(),
            create_new->bounds().CenterPoint().y());

  footer->SetPresentationCollapsed(false);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CreationControlIsIndependentOfCommandSearch) {
  SeoulOrganizationService* const organization = service();
  ASSERT_TRUE(organization);
  ASSERT_TRUE(organization->shell_service());
  SeoulShellFooterView* const footer =
      organization->shell_service()->GetFooterForTesting(WindowKey());
  ASSERT_TRUE(footer);
  views::View* const icon = footer->create_new_icon_for_testing();
  ASSERT_TRUE(icon);
  ASSERT_TRUE(icon->layer());

  const bool originally_preferred_reduced_motion =
      gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_reduced_motion(base::BindOnce(
      [](bool value) {
        gfx::Animation::SetPrefersReducedMotionForTesting(value);
      },
      originally_preferred_reduced_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_FALSE(icon->GetLocalBounds().IsEmpty());
  EXPECT_FALSE(footer->is_command_launcher_visible_for_testing());
  EXPECT_TRUE(icon->layer()->transform().IsIdentity());

  ASSERT_TRUE(footer->ShowCommandLauncher());
  EXPECT_TRUE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_TRUE(footer->is_command_launcher_visible_for_testing());
  EXPECT_TRUE(icon->layer()->transform().IsIdentity());
  EXPECT_FALSE(footer->is_create_menu_running_for_testing());

  // Command search toggles without changing the independent creation menu.
  ASSERT_TRUE(footer->ShowCommandLauncher());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(footer->is_command_launcher_visible_for_testing());
  EXPECT_TRUE(icon->layer()->transform().IsIdentity());

  ASSERT_TRUE(footer->ShowCommandLauncher());
  EXPECT_TRUE(browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(footer->is_command_launcher_visible_for_testing());
  EXPECT_TRUE(icon->layer()->transform().IsIdentity());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CreationMenuDispatchesAfterCloseAndPreservesDialogFocus) {
  auto* shell_service = service()->shell_service();
  ASSERT_TRUE(shell_service);
  auto* footer = shell_service->GetFooterForTesting(WindowKey());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(footer);
  ASSERT_TRUE(view);
  ui::test::EventGenerator events = MakeEventGenerator();
  const int original_count = browser()->tab_strip_model()->count();
  auto* const original_page =
      browser()->tab_strip_model()->GetActiveWebContents();
  const auto original_workspace =
      service()->model().ActiveWorkspaceForWindow(WindowKey().value());
  auto open_menu = [&]() {
    view->GetWidget()->LayoutRootViewIfNecessary();
    events.SetTargetWindow(views::GetRootWindow(view->GetWidget()));
    events.MoveMouseTo(
        footer->create_new_button_for_testing()->GetBoundsInScreen().CenterPoint());
    events.ClickLeftButton();
    return base::test::RunUntil(
        [&]() { return footer->is_create_menu_running_for_testing(); });
  };
  ASSERT_TRUE(open_menu());
  EXPECT_FALSE(view->IsSeoulOmniboxActionMode());
  events.PressAndReleaseKey(ui::VKEY_DOWN);
  auto* menu = views::MenuController::GetActiveInstance();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetSelectedMenuItem());
  EXPECT_EQ(u"New tab", menu->GetSelectedMenuItem()->title());
  events.SetTargetWindow(
      views::GetRootWindow(menu->GetSelectedMenuItem()->GetWidget()));
  events.MoveMouseTo(menu->GetSelectedMenuItem()->GetBoundsInScreen().CenterPoint());
  events.ClickLeftButton();
  auto* omnibox = view->toolbar()->location_bar_view()->omnibox_view();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return view->is_seoul_new_tab_surface_pending() && omnibox->HasFocus();
  }));
  EXPECT_EQ(original_count, browser()->tab_strip_model()->count());
  EXPECT_EQ(original_page,
            browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_FALSE(footer->is_create_menu_running_for_testing());
  omnibox->SetUserText(u"about:blank#menu-new-tab");
  events.SetTargetWindow(views::GetRootWindow(view->GetWidget()));
  events.PressAndReleaseKey(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() == original_count + 1;
  }));
  auto* destination = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(destination));
  EXPECT_EQ(GURL("about:blank#menu-new-tab"),
            destination->GetLastCommittedURL());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !view->is_seoul_new_tab_surface_pending(); }));
  EXPECT_FALSE(footer->is_create_menu_running_for_testing());

  ASSERT_TRUE(open_menu());
  for (int i = 0; i < 3; ++i)
    events.PressAndReleaseKey(ui::VKEY_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    menu = views::MenuController::GetActiveInstance();
    return menu && menu->GetSelectedMenuItem() &&
           menu->GetSelectedMenuItem()->title() == u"New container space";
  }));
  events.PressAndReleaseKey(ui::VKEY_RETURN);
  views::Widget* dialog = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    for (const auto& candidate : views::Widget::GetAllOwnedWidgets(
             view->GetWidget()->GetNativeView())) {
      if (candidate->IsVisible() && candidate->widget_delegate() &&
          candidate->widget_delegate()->GetWindowTitle() ==
              u"New Container Space") {
        dialog = candidate.get();
        return true;
      }
    }
    return false;
  }));
  EXPECT_FALSE(footer->is_create_menu_running_for_testing());
  ASSERT_TRUE(dialog->GetFocusManager());
  views::Textfield* name = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    name = views::AsViewClass<views::Textfield>(
        dialog->GetFocusManager()->GetFocusedView());
    return name != nullptr;
  }));
  name->InsertOrReplaceText(u"Menu container");
  auto* delegate = dialog->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  delegate->AcceptDialog();
  auto* controller = shell_service->GetController(WindowKey());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->snapshot().workspace.name == "Menu container" &&
           controller->snapshot().status == ShellStatus::kCoherent;
  }));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_FALSE(contents->GetPrimaryMainFrame()->GetStoragePartition()->
                   GetConfig().is_default());
  EXPECT_EQ(original_count + 2, browser()->tab_strip_model()->count());
  const auto container_workspace =
      service()->model().ActiveWorkspaceForWindow(WindowKey().value());
  EXPECT_NE(original_workspace, container_workspace);
  class SwitchProbe : public WorkspaceSwitchObserver {
   public:
    void OnWorkspaceSwitchPhaseChanged(
        WorkspaceSwitchPhase phase,
        std::optional<ProjectionError> error) override {
      if (error)
        last_error = ProjectionErrorToString(*error);
    }
    std::string last_error;
  } probe;
  base::ScopedObservation<WorkspaceSwitcher, WorkspaceSwitchObserver>
      observation(&probe);
  observation.Observe(service()->projection_service()->GetSwitcher(WindowKey()));
  ASSERT_TRUE(controller->SwitchWorkspace(original_workspace).has_value());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->snapshot().workspace.workspace_id == original_workspace &&
           controller->snapshot().status == ShellStatus::kCoherent;
  }));
  ASSERT_TRUE(controller->SwitchWorkspace(container_workspace).has_value())
      << probe.last_error;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->snapshot().workspace.workspace_id == container_workspace &&
           controller->snapshot().status == ShellStatus::kCoherent;
  }));
  EXPECT_EQ(contents, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(original_count + 2, browser()->tab_strip_model()->count());
  EXPECT_FALSE(controller->snapshot().show_status_banner);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       GroupedSplitsSurviveLayoutsAndPaneClose) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* strip = browser()->tab_strip_model();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  std::vector<content::WebContents*> pages;
  for (const auto* suffix : {"?first", "?second", "?third"}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(),
        embedded_test_server()->GetURL(std::string("/empty.html") + suffix),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    pages.push_back(strip->GetActiveWebContents());
  }
  const auto first_key = TabStripBridge::KeyForContents(pages[0]);
  const auto second_key = TabStripBridge::KeyForContents(pages[1]);
  const auto membership =
      service()->model().FindMembershipIdByTabKey(first_key.value());
  ASSERT_TRUE(membership.is_valid());
  const auto group =
      strip->AddToNewGroup({strip->GetIndexOfWebContents(pages[0]),
                            strip->GetIndexOfWebContents(pages[1])});
  strip->ActivateTabAt(strip->GetIndexOfWebContents(pages[0]));
  const auto split =
      strip->AddToNewSplit({strip->GetIndexOfWebContents(pages[1])},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  ASSERT_TRUE(base::test::RunUntil([&] { return view->IsInSplitView(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service()
        ->model()
        .FindSplitIdByUpstreamToken(split.ToString())
        .is_valid();
  }));
  for (const auto mode :
       {SeoulLayoutMode::kMultiple, SeoulLayoutMode::kCollapsed,
        SeoulLayoutMode::kSingle}) {
    view->SetSeoulLayoutMode(mode);
    view->GetWidget()->LayoutRootViewIfNecessary();
    EXPECT_TRUE(view->IsInSplitView());
    EXPECT_EQ(group,
              strip->GetTabGroupForTab(strip->GetIndexOfWebContents(pages[0])));
    EXPECT_EQ(split,
              strip->GetSplitForTab(strip->GetIndexOfWebContents(pages[1])));
    EXPECT_EQ(first_key, TabStripBridge::KeyForContents(pages[0]));
    EXPECT_EQ(second_key, TabStripBridge::KeyForContents(pages[1]));
    EXPECT_EQ(membership,
              service()->model().FindMembershipIdByTabKey(first_key.value()));
  }
  strip->RemoveSplit(split);
  EXPECT_FALSE(view->IsInSplitView());
  EXPECT_EQ(group,
            strip->GetTabGroupForTab(strip->GetIndexOfWebContents(pages[0])));
  EXPECT_EQ(group,
            strip->GetTabGroupForTab(strip->GetIndexOfWebContents(pages[1])));
  strip->ActivateTabAt(strip->GetIndexOfWebContents(pages[0]));
  const auto closing_split =
      strip->AddToNewSplit({strip->GetIndexOfWebContents(pages[1])},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kTabContextMenu);
  const int count_before_close = strip->count();
  strip->CloseWebContentsAt(strip->GetIndexOfWebContents(pages[1]),
                            TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(view->IsInSplitView());
  EXPECT_FALSE(strip->GetSplitForTab(strip->GetIndexOfWebContents(pages[0])));
  EXPECT_FALSE(service()
                   ->model()
                   .FindSplitIdByUpstreamToken(closing_split.ToString())
                   .is_valid());
  EXPECT_EQ(group,
            strip->GetTabGroupForTab(strip->GetIndexOfWebContents(pages[0])));
  EXPECT_EQ(first_key, TabStripBridge::KeyForContents(pages[0]));
  EXPECT_EQ(count_before_close - 1, strip->count());
  EXPECT_GE(strip->GetIndexOfWebContents(pages[2]), 0);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SettingsLayoutControlsChangeTheActualBrowser) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      window.seoulFind = function find(selector, root = document) {
        const direct = root.querySelector(selector);
        if (direct) return direct;
        for (const node of root.querySelectorAll('*')) {
          if (node.shadowRoot) {
            const found = find(selector, node.shadowRoot);
            if (found) return found;
          }
        }
        return null;
      };
      for (let i = 0; i < 200; ++i) {
        const group = seoulFind('#seoulLayout');
        if (group?.pref?.value !== undefined) return true;
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      return false;
    })()
  )JS"));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  for (const int mode : {1, 2, 0}) {
    ASSERT_TRUE(content::ExecJs(
        contents, content::JsReplace("seoulFind('#seoulLayout').querySelector('"
                                     "[name=\"' + $1 + '\"]').click();",
                                     mode)));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return browser()->profile()->GetPrefs()->GetInteger(
                 kSeoulLayoutModePref) == mode;
    }));
    view->GetWidget()->LayoutRootViewIfNecessary();
    EXPECT_EQ(mode, static_cast<int>(view->seoul_layout_mode()));
    auto* field = view->toolbar()->location_bar_view();
    EXPECT_EQ(10, field->GetBorderRadius());
    EXPECT_LT(field->GetBorderRadius(), field->height() / 2);
    if (mode == 1) {
      auto* new_tab = view->vertical_tab_strip_region_view_for_testing()
                          ->GetBottomContainer();
      ASSERT_TRUE(new_tab);
      EXPECT_GE(new_tab->GetBoundsInScreen().y(),
                view->toolbar()->GetBoundsInScreen().bottom())
          << "New Tab must stay below the native window-control row";
    }
  }
  // An external preference change must update the selected control as well.
  browser()->profile()->GetPrefs()->SetInteger(kSeoulLayoutModePref, 1);
  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      for (let i = 0; i < 200; ++i) {
        if (seoulFind('#seoulLayout').selected === '1') return true;
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      return false;
    })()
  )JS"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/downloads")));
  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const find = (selector, root = document) => {
        const direct = root.querySelector(selector);
        if (direct) return direct;
        for (const node of root.querySelectorAll('*')) {
          const found = node.shadowRoot && find(selector, node.shadowRoot);
          if (found) return found;
        }
        return null;
      };
      for (let i = 0; i < 200; ++i) {
        if (find('#seoulDownloadAnimation')?.pref?.value !== undefined) return true;
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      return false;
    })()
  )JS"));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       DownloadsSearchRemoveAndUndoPreserveTheFile) {
  base::ScopedTempDir files;
  ASSERT_TRUE(files.CreateUniqueTempDir());
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* manager = browser()->profile()->GetDownloadManager();
  content::DownloadTestObserverTerminal observer(
      manager, 1, content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  auto params =
      content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
          browser()->tab_strip_model()->GetActiveWebContents(),
          embedded_test_server()->GetURL("/empty.html"),
          TRAFFIC_ANNOTATION_FOR_TESTS);
  browser()->profile()->GetPrefs()->SetFilePath(prefs::kDownloadDefaultDirectory,
                                               files.GetPath());
  params->set_suggested_name(u"seoul-download-check.txt");
  params->set_prompt(false);
  manager->DownloadUrl(std::move(params));
  observer.WaitForFinished();
  ASSERT_EQ(1u,
            observer.NumDownloadsSeenInState(download::DownloadItem::COMPLETE));
  content::DownloadManager::DownloadVector downloads;
  manager->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  ASSERT_FALSE(downloads.front()->IsTemporary());
  const auto path = downloads.front()->GetTargetFilePath();
  EXPECT_EQ(files.GetPath(), path.DirName());
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://downloads/")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      window.seoulFind = function find(selector, root = document) {
        const direct = root.querySelector(selector);
        if (direct) return direct;
        for (const node of root.querySelectorAll('*')) {
          if (node.shadowRoot) {
            const found = find(selector, node.shadowRoot);
            if (found) return found;
          }
        }
        return null;
      };
      window.seoulWait = async (predicate, stage) => {
        for (let i = 0; i < 200; ++i) {
          if (predicate()) return true;
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        throw new Error(stage + ': ' + JSON.stringify({
          items: seoulFind('downloads-manager')?.items_?.length,
          item: seoulFind('downloads-item')?.data,
          visible: seoulFind('downloads-item')?.checkVisibility(),
          listHidden: seoulFind('#downloadsList')?.hidden,
          toast: seoulFind('cr-toast-manager')?.isToastOpen,
        }));
      };
      await seoulWait(() => seoulFind('downloads-item')?.checkVisibility(), 'initial file');
      const toolbar = seoulFind('downloads-toolbar');
      const search = toolbar.shadowRoot.querySelector('#search');
      search.setValue('no-matching-download-9127');
      await seoulWait(() => !seoulFind('downloads-item')?.checkVisibility(), 'search');
      search.setValue('');
      await seoulWait(() => seoulFind('downloads-item')?.checkVisibility(), 'clear search');
      seoulFind('#quick-remove').click();
      await seoulWait(() => !seoulFind('downloads-item')?.checkVisibility(), 'remove');
      seoulFind('cr-toast-manager').querySelector('cr-button').click();
      return seoulWait(() => seoulFind('downloads-item')?.checkVisibility(), 'undo');
    })()
  )JS"));
  base::ScopedAllowBlockingForTesting allow_blocking;
  EXPECT_TRUE(base::PathExists(path));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       AppearanceLayoutModesAreReversibleAtNarrowWidth) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(vertical_region);
  ASSERT_TRUE(controller);
  ToolbarView* const original_toolbar = browser_view->toolbar();
  LocationBarView* const original_location_bar =
      original_toolbar->location_bar_view();
  ASSERT_TRUE(original_location_bar);

  EXPECT_EQ(seoul::SeoulLayoutMode::kSingle, browser_view->seoul_layout_mode());
  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());
  EXPECT_EQ(static_cast<views::View*>(vertical_region),
            original_toolbar->parent());

  // Compact is independent of Appearance. Enable it after the rail is already
  // durably collapsed to cover the real preference-toggle ordering; the
  // presentation must hide immediately without another collapse transition.
  controller->RequestCollapse(true);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsCollapsed(); }));
  controller->SetExpandOnHoverEnabledForWindow(true);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            vertical_region->width());
  EXPECT_EQ(8, browser_view->contents_container()->x());
  EXPECT_TRUE(vertical_region->layer()->GetMasksToBounds())
      << "Enabling Compact after an existing collapse must clip immediately";

  const gfx::Rect original_bounds = browser_view->bounds();
  browser_view->SetBoundsRect(gfx::Rect(0, 0, 640, 500));

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kMultiple);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsCollapsed(); }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(original_location_bar,
            browser_view->toolbar()->location_bar_view());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            browser_view->toolbar()->parent());
  EXPECT_FALSE(browser_view->toolbar()->is_seoul_sidebar_presentation());
  EXPECT_FALSE(controller->IsExpandOnHoverEnabled());
  EXPECT_TRUE(browser_view->toolbar()->GetVisible());
  EXPECT_TRUE(original_location_bar->GetVisible());
  EXPECT_GT(browser_view->toolbar()->width(), 0);
  EXPECT_GE(browser_view->contents_container()->x(),
            vertical_region->bounds().right());
  EXPECT_GE(browser_view->contents_container()->y(),
            browser_view->top_container()->y() +
                browser_view->toolbar()->bounds().bottom());

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kCollapsed);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsCollapsed(); }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            browser_view->toolbar()->parent());
  EXPECT_FALSE(controller->IsExpandOnHoverEnabled());
  EXPECT_FALSE(vertical_region->is_expanded_on_hover());
  EXPECT_EQ(VerticalTabStripRegionView::kCollapsedWidth,
            vertical_region->width());
  EXPECT_FALSE(vertical_region->layer()->GetMasksToBounds())
      << "The ordinary 60-DIP rail must never use Compact clipping";
  EXPECT_GE(browser_view->contents_container()->x(),
            vertical_region->bounds().right());

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kSingle);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsCollapsed(); }));
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(original_location_bar,
            browser_view->toolbar()->location_bar_view());
  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());
  EXPECT_TRUE(controller->IsExpandOnHoverEnabled())
      << "Compact state must be restored after durable layout modes";
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            vertical_region->width())
      << "Single + Compact must leave only Zen's edge reveal target";
  EXPECT_LT(vertical_region->width(),
            VerticalTabStripRegionView::kCollapsedWidth);
  EXPECT_EQ(8, browser_view->contents_container()->x())
      << "The hidden compact rail must not reserve a permanent icon column";

  controller->SetExpandOnHoverEnabledForWindow(false);
  controller->RequestCollapse(false);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsCollapsed(); }));
  browser_view->SetBoundsRect(original_bounds);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       PointerSelectsTabsAcrossExpandedAndCollapsedLayouts) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  auto* animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(view);
  ASSERT_TRUE(state);
  ASSERT_TRUE(animations);
  auto* strip = browser()->tab_strip_model();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Tab0</title>")));
  for (int i = 1; i < 5; ++i) {
    chrome::AddTabAt(browser(),
                     GURL("data:text/html,<title>Tab" +
                          base::NumberToString(i) + "</title>"),
                     -1, true);
    ASSERT_TRUE(content::WaitForLoadStop(strip->GetActiveWebContents()));
  }
  ui::test::EventGenerator events = MakeEventGenerator();
  for (auto mode : {SeoulLayoutMode::kSingle, SeoulLayoutMode::kCollapsed,
                    SeoulLayoutMode::kSingle}) {
    state->SetExpandOnHoverEnabledForWindow(false);
    view->SetSeoulLayoutMode(mode);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
    }));
    for (int index : {0, 4, 1, 3, 2, 0, 4, 2, 1, 3}) {
      SCOPED_TRACE(base::NumberToString(static_cast<int>(mode)) + ":" +
                   base::NumberToString(index));
      view->GetWidget()->LayoutRootViewIfNecessary();
      std::vector<VerticalTabView*> visible_tabs;
      CollectVerticalTabViews(
          view->vertical_tab_strip_region_view_for_testing(), &visible_tabs);
      auto tab = std::ranges::find_if(visible_tabs, [&](auto* item) {
        return item->data().title == u"Tab" + base::NumberToString16(index);
      });
      ASSERT_NE(tab, visible_tabs.end());
      ASSERT_TRUE((*tab)->IsDrawn());
      ASSERT_GE((*tab)->GetVisibleBounds().width(), 20);
      events.MoveMouseTo((*tab)->GetBoundsInScreen().CenterPoint());
      events.ClickLeftButton();
      ASSERT_TRUE(base::test::RunUntil([&] {
        return strip->active_index() == index && (*tab)->IsActive();
      }));
      EXPECT_EQ(5, strip->count());
    }
  }
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       PRE_CollapsedStartupKeepsTabsReachable) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(view);
  ASSERT_TRUE(controller);
  controller->SetUncollapsedWidth(500);
  view->SetSeoulLayoutMode(SeoulLayoutMode::kCollapsed);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsCollapsed(); }));
  browser()->profile()->GetPrefs()->CommitPendingWrite();
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CollapsedStartupKeepsTabsReachable) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(view);
  ASSERT_EQ(SeoulLayoutMode::kCollapsed, view->seoul_layout_mode());
  ASSERT_EQ(
      ShellMode::kCollapsed,
      service()->shell_service()->GetController(WindowKey())->snapshot().mode);
  ASSERT_GT(browser()->tab_strip_model()->count(), 0);
  view->GetWidget()->LayoutRootViewIfNecessary();
  std::vector<VerticalTabView*> tabs;
  CollectVerticalTabViews(view->vertical_tab_strip_region_view_for_testing(),
                          &tabs);
  ASSERT_FALSE(tabs.empty());
  EXPECT_TRUE(std::ranges::any_of(tabs, [](auto* tab) {
    return tab->IsDrawn() && tab->GetVisibleBounds().width() >= 20 &&
           tab->GetVisibleBounds().height() >= 20;
  })) << "Restoring a collapsed layout must retain a visible current tab";
  views::Button* expand = nullptr;
  shared::NewTabButton* new_tab = nullptr;
  std::vector<views::View*> pending{
      view->vertical_tab_strip_region_view_for_testing()};
  while (!pending.empty()) {
    auto* child = pending.back();
    pending.pop_back();
    if (child->GetViewAccessibility().GetCachedName() == u"Expand sidebar")
      expand = views::AsViewClass<views::LabelButton>(child);
    if (views::IsViewClass<shared::NewTabButton>(child))
      new_tab = views::AsViewClass<shared::NewTabButton>(child);
    for (views::View* descendant : child->children())
      pending.push_back(descendant);
  }
  ASSERT_TRUE(new_tab);
  EXPECT_TRUE(new_tab->IsDrawn());
  EXPECT_GE(new_tab->GetVisibleBounds().width(), 20);
  EXPECT_TRUE(new_tab->GetText().empty());
  ASSERT_TRUE(expand);
  ASSERT_TRUE(expand->IsDrawn());
  ui::test::EventGenerator events = MakeEventGenerator();
  events.MoveMouseTo(expand->GetBoundsInScreen().CenterPoint());
  events.ClickLeftButton();
  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsCollapsed(); }));
  EXPECT_TRUE(view->IsSeoulToolbarIntegrated());
  EXPECT_EQ(SeoulLayoutMode::kSingle, view->seoul_layout_mode());
  EXPECT_FALSE(expand->IsDrawn());
  view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(new_tab->GetText().empty());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CompactShortcutTogglesExactlyOncePerPress) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::FocusManager* const focus_manager = browser_view->GetFocusManager();
  ASSERT_TRUE(focus_manager);
  auto* const vertical_tabs =
      tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(vertical_tabs);

  SeoulOrganizationService* const svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->shell_service());
  ShellController* const shell =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(shell);
  ASSERT_TRUE(shell->snapshot().compact_mode.available);

  const ui::Accelerator compact(ui::VKEY_S, ui::EF_PLATFORM_ACCELERATOR);
  EXPECT_TRUE(focus_manager->HasPriorityHandler(compact));
  const bool initially_enabled = shell->snapshot().compact_mode.enabled;

  EXPECT_TRUE(focus_manager->ProcessAccelerator(compact));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return shell->snapshot().compact_mode.enabled != initially_enabled;
  }));
  EXPECT_EQ(!initially_enabled, vertical_tabs->IsExpandOnHoverEnabled());

  EXPECT_TRUE(focus_manager->ProcessAccelerator(compact));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return shell->snapshot().compact_mode.enabled == initially_enabled;
  }));
  EXPECT_EQ(initially_enabled, vertical_tabs->IsExpandOnHoverEnabled());
}

IN_PROC_BROWSER_TEST_F(
    SeoulShellBrowserTest,
    CompactMultipleRoundTripKeepsFiveDipEndpointAndPresentation) {
  gfx::ScopedAnimationDurationScaleMode animation_duration(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const controller =
      tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(controller);
  ASSERT_TRUE(region);

  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  auto* const animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->IsCollapsed() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->width());
  EXPECT_TRUE(region->layer()->GetMasksToBounds())
      << "The five-DIP endpoint must clip retained shell controls";

  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kMultiple);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  EXPECT_TRUE(region->IsSeoulCompactExitAnimation());
  EXPECT_FALSE(region->layer()->GetMasksToBounds())
      << "Compact exit must unclip before its first reveal frame";
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->GetPreferredSize().width());

  int previous_width = region->GetPreferredSize().width();
  bool monotonic = true;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    const int current_width = region->GetPreferredSize().width();
    monotonic &= current_width >= previous_width;
    previous_width = current_width;
    return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  EXPECT_TRUE(monotonic);
  EXPECT_EQ(controller->GetUncollapsedWidth(),
            region->GetPreferredSize().width());

  // Returning to Single restores the deferred Compact state. Hover semantics
  // must be restored before collapse starts so the complete sidebar remains
  // painted during travel and hides only at the five-DIP endpoint.
  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kSingle);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  EXPECT_TRUE(controller->IsExpandOnHoverEnabled());
  EXPECT_TRUE(browser_view->toolbar()->GetVisible());
  EXPECT_FALSE(region->layer()->GetMasksToBounds())
      << "Returning to Compact must remain unmasked while the rail travels";
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(controller->IsCollapsed());
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->width());
  EXPECT_TRUE(region->layer()->GetMasksToBounds());
  EXPECT_FALSE(browser_view->toolbar()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CompactCollapsedMultipleStartsFromRealSixtyDipEndpoint) {
  gfx::ScopedAnimationDurationScaleMode animation_duration(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const controller =
      tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(controller);
  ASSERT_TRUE(region);

  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  auto* const animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->IsCollapsed() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));

  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kCollapsed);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_FALSE(region->IsSeoulCompactExitAnimation());
  ASSERT_EQ(VerticalTabStripRegionView::kCollapsedWidth, region->width());

  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kMultiple);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  EXPECT_FALSE(region->IsSeoulCompactExitAnimation());
  EXPECT_EQ(VerticalTabStripRegionView::kCollapsedWidth,
            region->GetPreferredSize().width());

  int previous_width = region->GetPreferredSize().width();
  bool monotonic = true;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    const int current_width = region->GetPreferredSize().width();
    monotonic &= current_width >= previous_width;
    previous_width = current_width;
    return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  EXPECT_TRUE(monotonic);
  EXPECT_EQ(controller->GetUncollapsedWidth(),
            region->GetPreferredSize().width());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CompactHoverRevealAndReturnCollapseClipOnlyAtEndpoints) {
  gfx::ScopedAnimationDurationScaleMode animation_duration(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const controller =
      tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  auto* const animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(region);
  ASSERT_TRUE(animations);

  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->IsCollapsed() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->width());
  ASSERT_TRUE(region->layer()->GetMasksToBounds());

  auto paint_as_active = browser_view->GetWidget()->LockPaintAsActive();
  const gfx::Point hover_point = region->GetLocalBounds().CenterPoint();
  const ui::MouseEvent mouse_enter(ui::EventType::kMouseEntered, hover_point,
                                   hover_point, base::TimeTicks::Now(),
                                   ui::EF_NONE, ui::EF_NONE);
  region->OnMouseEntered(mouse_enter);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  EXPECT_FALSE(region->layer()->GetMasksToBounds())
      << "Hover reveal must unclip before its first animation frame";
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    return region->is_expanded_on_hover() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  EXPECT_EQ(controller->GetUncollapsedWidth(), region->width());
  EXPECT_FALSE(region->layer()->GetMasksToBounds());

  const gfx::Point exit_point(-1, -1);
  const ui::MouseEvent mouse_exit(ui::EventType::kMouseExited, exit_point,
                                  exit_point, base::TimeTicks::Now(),
                                  ui::EF_NONE, ui::EF_NONE);
  region->OnMouseExited(mouse_exit);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    return !region->is_expanded_on_hover() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->width());
  EXPECT_TRUE(region->layer()->GetMasksToBounds())
      << "Hover return must clip only after reaching the five-DIP endpoint";
  EXPECT_FALSE(browser_view->toolbar()->GetVisible());
}

// Address focus opens the floating search field without holding the rail open.
// Tab focus must still expand the rail for keyboard navigation.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ExpandOnHoverSeparatesAddressFocusFromTabFocus) {
  // Exercise docked page controls, after leaving the startup search surface.
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const controller =
      tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  auto* const animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(region);
  ASSERT_TRUE(animations);
  ASSERT_EQ(region, browser_view->toolbar()->parent())
      << "this case is only meaningful while the rail hosts the toolbar";

  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->IsCollapsed() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  ASSERT_FALSE(region->is_expanded_on_hover());

  // Taking address focus reparents the omnibox into the floating surface.
  OmniboxViewViews* const omnibox =
      browser_view->toolbar()->location_bar_view()->omnibox_view();
  ASSERT_TRUE(omnibox);
  browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true);
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(omnibox, browser_view->GetFocusManager()->GetFocusedView());
  ASSERT_TRUE(browser_view->IsViewInSeoulOmniboxSurface(omnibox));
  EXPECT_FALSE(region->is_expanded_on_hover())
      << "omnibox focus must not hold the rail open";
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(VerticalTabStripRegionView::kCompactCollapsedWidth,
            region->width());

  // Focus on a tab still does, exactly as upstream intends.
  std::vector<VerticalTabView*> tabs;
  CollectVerticalTabViews(region, &tabs);
  ASSERT_FALSE(tabs.empty());
  tabs.front()->RequestFocus();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(tabs.front(), browser_view->GetFocusManager()->GetFocusedView())
      << "the tab must really take focus, or this proves nothing";
  EXPECT_TRUE(region->is_expanded_on_hover())
      << "tab-strip focus must still hold the rail open";
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       HoverExpandedCompactExitKeepsRailAndContentContinuous) {
  gfx::ScopedAnimationDurationScaleMode animation_duration(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const controller =
      tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  auto* const animations = BrowserAnimationController::From(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(region);
  ASSERT_TRUE(animations);

  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return controller->IsCollapsed() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));

  // The region itself is intentionally not focusable. Keep the otherwise
  // inactive browser-test widget in the same frame-active state required by
  // production hover handling, then deliver the real mouse-enter callback.
  auto paint_as_active = browser_view->GetWidget()->LockPaintAsActive();
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(browser_view->GetWidget()->ShouldPaintAsActive());
  ASSERT_FALSE(region->GetLocalBounds().IsEmpty());
  const gfx::Point hover_point = region->GetLocalBounds().CenterPoint();
  const ui::MouseEvent mouse_enter(ui::EventType::kMouseEntered, hover_point,
                                   hover_point, base::TimeTicks::Now(),
                                   ui::EF_NONE, ui::EF_NONE);
  region->OnMouseEntered(mouse_enter);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    return region->is_expanded_on_hover() &&
           !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  const int revealed_width = region->width();
  const int compact_content_x = browser_view->contents_container()->x();
  ASSERT_EQ(controller->GetUncollapsedWidth(), revealed_width);
  ASSERT_EQ(8, compact_content_x);

  // Disabling Compact first starts hover collapse. The following durable
  // expand replaces that motion synchronously, which must not discard the
  // armed five-DIP logical endpoint.
  controller->SetExpandOnHoverEnabledForWindow(false);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  controller->RequestCollapse(false);
  ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  ASSERT_TRUE(region->IsSeoulCompactExitAnimation());
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_NEAR(revealed_width, region->width(), 1);
  EXPECT_NEAR(compact_content_x, browser_view->contents_container()->x(), 1);

  int previous_content_x = browser_view->contents_container()->x();
  bool content_moved_monotonically = true;
  bool compact_exit_cleared_early = false;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    const bool is_animating =
        animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
    const int content_x = browser_view->contents_container()->x();
    content_moved_monotonically &= content_x >= previous_content_x;
    previous_content_x = content_x;
    compact_exit_cleared_early |=
        is_animating && !region->IsSeoulCompactExitAnimation();
    return !is_animating;
  }));
  EXPECT_TRUE(content_moved_monotonically);
  EXPECT_FALSE(compact_exit_cleared_early);
  EXPECT_FALSE(region->IsSeoulCompactExitAnimation());
  EXPECT_EQ(controller->GetUncollapsedWidth(), region->width());
}

IN_PROC_BROWSER_TEST_F(
    SeoulShellBrowserTest,
    AppearanceTransitionsPreserveOmniboxAndHorizontalEscape) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* controller = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(controller);
  ToolbarView* const original_toolbar = browser_view->toolbar();
  LocationBarView* const original_location_bar =
      original_toolbar->location_bar_view();
  ASSERT_TRUE(original_location_bar);

  controller->SetExpandOnHoverEnabledForWindow(false);
  controller->RequestCollapse(false);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsCollapsed(); }));

  // Exercise the user-visible Cmd+L path. Programmatic focus intentionally
  // leaves the integrated address row docked so startup/background work cannot
  // surface browser chrome or steal focus.
  browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true);
  ASSERT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());
  ASSERT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            original_location_bar->parent());

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kMultiple);
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(original_location_bar,
            browser_view->toolbar()->location_bar_view());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            original_toolbar->parent());
  EXPECT_EQ(static_cast<views::View*>(original_toolbar),
            original_location_bar->parent());
  EXPECT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kCollapsed);
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());

  browser_view->SetSeoulLayoutMode(seoul::SeoulLayoutMode::kSingle);
  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            original_location_bar->parent());
  EXPECT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());

  controller->SetVerticalTabsEnabled(false);
  EXPECT_FALSE(controller->ShouldDisplayVerticalTabs());
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            original_toolbar->parent());
  EXPECT_FALSE(original_toolbar->is_seoul_sidebar_presentation());
  EXPECT_EQ(static_cast<views::View*>(original_toolbar),
            original_location_bar->parent());
  EXPECT_TRUE(original_location_bar->GetVisible());

  controller->SetVerticalTabsEnabled(true);
  EXPECT_TRUE(controller->ShouldDisplayVerticalTabs());
  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());
  EXPECT_EQ(original_toolbar, browser_view->toolbar());
  EXPECT_EQ(original_location_bar,
            browser_view->toolbar()->location_bar_view());
  EXPECT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       AppearanceLayoutsRemainReachableThroughUnifiedActions) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);

  struct ExpectedMode {
    std::string_view query;
    SeoulLayoutMode mode;
  };
  for (const ExpectedMode& expected : {
           ExpectedMode{"multiple toolbar", SeoulLayoutMode::kMultiple},
           ExpectedMode{"collapsed toolbar", SeoulLayoutMode::kCollapsed},
           ExpectedMode{"single toolbar", SeoulLayoutMode::kSingle},
       }) {
    browser_view->ShowSeoulOmniboxActions();
    SeoulOmniboxActionView* actions =
        browser_view->seoul_omnibox_action_view_for_testing();
    ASSERT_TRUE(actions);
    actions->SetQuery(expected.query);
    ASSERT_GE(actions->result_count(), 1u);
    ASSERT_TRUE(actions->ExecuteSelection());
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(expected.mode, browser_view->seoul_layout_mode());
    EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
    EXPECT_FALSE(browser_view->seoul_omnibox_action_view_for_testing());
  }

  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());
}

IN_PROC_BROWSER_TEST_F(
    SeoulShellBrowserTest,
    FloatingOmniboxClosesAcrossImmediateAppearanceTransitions) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);
  ASSERT_EQ(SeoulLayoutMode::kSingle, browser_view->seoul_layout_mode());

  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());
  ASSERT_TRUE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            location_bar->parent());

  // Switch modes synchronously, before the floating surface or its glow has
  // time to settle. The address bar must be restored before the ToolbarView is
  // reparented.
  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kMultiple);
  EXPECT_EQ(SeoulLayoutMode::kMultiple, browser_view->seoul_layout_mode());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(browser_view->seoul_omnibox_action_view_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            toolbar->parent());

  browser_view->SetSeoulLayoutMode(SeoulLayoutMode::kCollapsed);
  EXPECT_EQ(SeoulLayoutMode::kCollapsed, browser_view->seoul_layout_mode());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(browser_view->seoul_omnibox_action_view_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());
  EXPECT_EQ(static_cast<views::View*>(browser_view->top_container()),
            toolbar->parent());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       InvalidAppearanceLayoutFailsSafelyToSingle) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  PrefService* prefs = browser()->profile()->GetPrefs();

  prefs->SetInteger(seoul::kSeoulLayoutModePref, 99);
  EXPECT_EQ(seoul::SeoulLayoutMode::kSingle, seoul::GetSeoulLayoutMode(prefs));
  EXPECT_EQ(seoul::SeoulLayoutMode::kSingle, browser_view->seoul_layout_mode());
  EXPECT_TRUE(browser_view->IsSeoulToolbarIntegrated());

  browser_view->SetSeoulLayoutMode(static_cast<seoul::SeoulLayoutMode>(99));
  EXPECT_EQ(static_cast<int>(seoul::SeoulLayoutMode::kSingle),
            prefs->GetInteger(seoul::kSeoulLayoutModePref));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ContentsDoNotPaintUnderVerticalRail) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);
  ASSERT_TRUE(browser_view->contents_container());
  ASSERT_TRUE(browser_view->toolbar());
  browser_view->GetWidget()->LayoutRootViewIfNecessary();

  const gfx::Rect rail = vertical_region->bounds();
  const gfx::Rect contents = browser_view->contents_container()->bounds();
  EXPECT_GT(rail.width(), 0);
  EXPECT_GE(contents.x(), rail.right())
      << "rail=" << rail.ToString() << " contents=" << contents.ToString();
  EXPECT_FALSE(vertical_region->GetTopContainer()->GetVisible());

  // Zen Single Toolbar keeps both 38-DIP rows and their 4-DIP gap stable.
  EXPECT_EQ(80, browser_view->toolbar()->height());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ProgrammaticNewTabKeepsChromiumContract) {
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL), browser()->GetNewTabURL());
  const int previous_count = browser()->tab_strip_model()->count();
  chrome::NewTab(browser());
  ASSERT_EQ(previous_count + 1, browser()->tab_strip_model()->count());
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  // Both WebContents URL getters return the entry's VIRTUAL url, and the NTP
  // reverse-rewrite sets that back to chrome://newtab/ - so neither of them can
  // show which page actually committed. The real committed url lives on the
  // navigation entry, and that is what has to be Chromium's NTP: Seoul no
  // longer owns the new tab (see the Welcome/onboarding row in
  // docs/product/zen-chromium-parity.md).
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL), contents->GetVisibleURL());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL), contents->GetLastCommittedURL());
  content::NavigationEntry* const entry =
      contents->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(search::GetNewTabPageURL(browser()->profile()), entry->GetURL());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SyntheticPlaceholderNeverLoadsNormalProfileNtp) {
  content::WebContents* const placeholder = chrome::AddAndReturnTabAt(
      browser(), chrome::ChromeUINewTabURLAsGURL(), -1,
      /*foreground=*/true, std::nullopt, /*pinned=*/false,
      /*synthetic_new_tab_placeholder=*/true);
  ASSERT_TRUE(placeholder);
  ASSERT_TRUE(content::WaitForLoadStop(placeholder));
  EXPECT_EQ(GURL(url::kAboutBlankURL), placeholder->GetLastCommittedURL());
  EXPECT_EQ(GURL(url::kAboutBlankURL), placeholder->GetVisibleURL());
  EXPECT_TRUE(HasSyntheticNewTabPlaceholderProvenance(placeholder));
  EXPECT_FALSE(placeholder->IsLoading());

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  EXPECT_TRUE(location_bar->omnibox_view()->GetText().empty());
  EXPECT_EQ(ExpectedSeoulPlaceholder(browser()),
            location_bar->omnibox_view()->GetPlaceholderText());
  ASSERT_TRUE(location_bar->seoul_floating_search_icon_for_testing());
  EXPECT_TRUE(
      location_bar->seoul_floating_search_icon_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       OrdinaryAboutBlankKeepsChromiumPresentation) {
  content::WebContents* const ordinary_blank =
      chrome::AddAndReturnTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                                /*foreground=*/true);
  ASSERT_TRUE(ordinary_blank);
  ASSERT_TRUE(content::WaitForLoadStop(ordinary_blank));
  EXPECT_FALSE(HasSyntheticNewTabPlaceholderProvenance(ordinary_blank));

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  EXPECT_EQ(u"about:blank", location_bar->omnibox_view()->GetText());
  ASSERT_TRUE(location_bar->seoul_floating_search_icon_for_testing());
  EXPECT_FALSE(
      location_bar->seoul_floating_search_icon_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SyntheticPlaceholderProvenanceRoundTripsExactly) {
  content::WebContents* const placeholder = chrome::AddAndReturnTabAt(
      browser(), chrome::ChromeUINewTabURLAsGURL(), -1,
      /*foreground=*/true, std::nullopt, /*pinned=*/false,
      /*synthetic_new_tab_placeholder=*/true);
  ASSERT_TRUE(placeholder);
  ASSERT_TRUE(content::WaitForLoadStop(placeholder));

  std::map<std::string, std::string> metadata;
  PopulateSeoulSessionMetadata(placeholder, &metadata);
  ASSERT_TRUE(metadata.contains(kSeoulSyntheticNewTabPlaceholderSessionKey));
  EXPECT_EQ("1", metadata[kSeoulSyntheticNewTabPlaceholderSessionKey]);

  content::WebContents* const ordinary_blank =
      chrome::AddAndReturnTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                                /*foreground=*/true);
  ASSERT_TRUE(ordinary_blank);
  ASSERT_TRUE(content::WaitForLoadStop(ordinary_blank));
  std::map<std::string, std::string> ordinary_metadata;
  PopulateSeoulSessionMetadata(ordinary_blank, &ordinary_metadata);
  EXPECT_FALSE(
      ordinary_metadata.contains(kSeoulSyntheticNewTabPlaceholderSessionKey));

  RestoreSeoulSessionMetadata(ordinary_blank, metadata);
  EXPECT_TRUE(HasSyntheticNewTabPlaceholderProvenance(ordinary_blank));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL(chrome::kChromeUIVersionURL)));
  std::map<std::string, std::string> navigated_metadata;
  PopulateSeoulSessionMetadata(ordinary_blank, &navigated_metadata);
  EXPECT_FALSE(
      navigated_metadata.contains(kSeoulSyntheticNewTabPlaceholderSessionKey));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       RepeatedNewTabCommandPreservesInputAndSourcePage) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  TabStripModel* const tab_strip = browser()->tab_strip_model();
  content::WebContents* const source = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(source);
  const GURL source_url = source->GetVisibleURL();
  const int tab_count = tab_strip->count();
  const gfx::Rect contents_bounds =
      browser_view->contents_container()->bounds();

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(source, tab_strip->GetActiveWebContents());
  EXPECT_EQ(source_url, source->GetVisibleURL());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  ASSERT_TRUE(browser_view->seoul_omnibox_surface_for_testing());
  ASSERT_TRUE(browser_view->seoul_omnibox_toolbar_placeholder_for_testing());
  EXPECT_EQ(contents_bounds, browser_view->contents_container()->bounds());
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  EXPECT_TRUE(location_bar->seoul_floating_mode());
  EXPECT_TRUE(location_bar->omnibox_view()->GetText().empty());
  EXPECT_EQ(ExpectedSeoulPlaceholder(browser()),
            location_bar->omnibox_view()->GetPlaceholderText());

  location_bar->omnibox_view()->SetUserText(u"unfinished query",
                                           /*update_popup=*/false);
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(source, tab_strip->GetActiveWebContents());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_TRUE(browser_view->seoul_omnibox_toolbar_placeholder_for_testing());
  EXPECT_TRUE(location_bar->seoul_floating_mode());
  EXPECT_TRUE(location_bar->omnibox_view()->HasFocus());
  EXPECT_EQ(u"unfinished query", location_bar->omnibox_view()->GetText());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       NewTabSurfaceEscapeRestoresSourcePage) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  TabStripModel* const tab_strip = browser()->tab_strip_model();
  content::WebContents* const source = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(source);
  const GURL source_url = source->GetVisibleURL();
  const int tab_count = tab_strip->count();

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  OmniboxViewViews* const omnibox =
      browser_view->toolbar()->location_bar_view()->omnibox_view();
  ASSERT_TRUE(omnibox);
  browser()->window()->Activate();
  gfx::NativeWindow event_window = browser()->window()->GetNativeWindow();
#if defined(USE_AURA)
  event_window = event_window->GetRootWindow();
#endif
  ui::test::EventGenerator generator(event_window);
  generator.PressAndReleaseKey(ui::VKEY_ESCAPE);

  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(source, tab_strip->GetActiveWebContents());
  EXPECT_EQ(source_url, source->GetVisibleURL());
  EXPECT_FALSE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       NewTabSurfaceEnterCreatesForegroundTab) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  TabStripModel* const tab_strip = browser()->tab_strip_model();
  content::WebContents* const source = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(source);
  const int tab_count = tab_strip->count();

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  omnibox->SetUserText(u"chrome://version/", /*update_popup=*/true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return location_bar->GetOmniboxPopupViewForTesting()->IsOpen(); }));
  browser()->window()->Activate();
  gfx::NativeWindow event_window = browser()->window()->GetNativeWindow();
#if defined(USE_AURA)
  event_window = event_window->GetRootWindow();
#endif
  ui::test::EventGenerator generator(event_window);
  generator.PressAndReleaseKey(ui::VKEY_RETURN);

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tab_strip->count() == tab_count + 1; }));
  content::WebContents* const destination = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(destination);
  EXPECT_NE(source, destination);
  ASSERT_TRUE(content::WaitForLoadStop(destination));
  EXPECT_EQ(GURL("chrome://version/"), destination->GetLastCommittedURL());
  EXPECT_FALSE(browser_view->is_seoul_new_tab_surface_pending());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, NewTabActionUsesSameZenSurface) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  const int tab_count = browser()->tab_strip_model()->count();
  actions::ActionItem* const action = actions::ActionManager::Get().FindAction(
      kActionNewTab, browser()->GetActions()->root_action_item());
  ASSERT_TRUE(action);

  action->InvokeAction();
  EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing());

  action->InvokeAction();
  EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_TRUE(browser_view->toolbar()->location_bar_view()->omnibox_view()->
                  HasFocus());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       UnifiedOmniboxActionSurfaceFitsCompactWindow) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ASSERT_TRUE(browser_view->toolbar());

  const gfx::Rect original_bounds = browser_view->bounds();
  browser_view->SetBoundsRect(gfx::Rect(0, 0, 640, 500));
  browser_view->ShowSeoulOmniboxActions();
  browser_view->DeprecatedLayoutImmediately();

  LocationBarView* location_bar = browser_view->toolbar()->location_bar_view();
  auto* actions = browser_view->seoul_omnibox_action_view_for_testing();
  views::View* surface = browser_view->seoul_omnibox_surface_for_testing();
  views::View* backdrop = browser_view->seoul_omnibox_backdrop_for_testing();
  views::View* shadow = browser_view->seoul_omnibox_shadow_for_testing();
  views::View* glow = browser_view->seoul_omnibox_glow_for_testing();
  ASSERT_TRUE(location_bar);
  ASSERT_TRUE(actions);
  ASSERT_TRUE(surface);
  ASSERT_TRUE(backdrop);
  ASSERT_TRUE(shadow);
  ASSERT_TRUE(glow);
  ASSERT_TRUE(surface->layer());
  ASSERT_TRUE(backdrop->layer());
  ASSERT_TRUE(shadow->layer());
  ASSERT_TRUE(glow->layer());
  EXPECT_EQ(ui::LAYER_TEXTURED, surface->layer()->type());
  EXPECT_EQ(ui::LAYER_TEXTURED, backdrop->layer()->type());
  EXPECT_EQ(ui::LAYER_TEXTURED, shadow->layer()->type());
  EXPECT_EQ(ui::LAYER_TEXTURED, glow->layer()->type());
  EXPECT_FALSE(surface->layer()->fills_bounds_opaquely());
  EXPECT_FALSE(backdrop->layer()->fills_bounds_opaquely());
  EXPECT_FALSE(shadow->layer()->fills_bounds_opaquely());
  EXPECT_FALSE(glow->layer()->fills_bounds_opaquely());
  EXPECT_FALSE(surface->layer()->GetMasksToBounds());
  EXPECT_FALSE(location_bar->location_icon_view()->GetVisible());

  EXPECT_EQ(gfx::Rect(0, 0, 427, 62), location_bar->bounds());
  EXPECT_EQ(location_bar->x(), actions->x());
  EXPECT_EQ(location_bar->width(), actions->width());
  EXPECT_EQ(location_bar->bounds().bottom(), actions->y());
  EXPECT_LE(actions->height(), 270);
  EXPECT_EQ(gfx::Point(), location_bar->bounds().origin());
  EXPECT_EQ(gfx::Point(106, 83), surface->bounds().origin());
  EXPECT_EQ(location_bar->width(), surface->width());
  EXPECT_EQ(location_bar->height() + actions->height(), surface->height());
  EXPECT_TRUE(browser_view->GetLocalBounds().Contains(surface->bounds()));
  EXPECT_TRUE(shadow->bounds().Contains(surface->GetLocalBounds()));
  EXPECT_TRUE(glow->bounds().Contains(surface->GetLocalBounds()));
  EXPECT_LT(shadow->x(), 0);
  EXPECT_LT(shadow->y(), 0);
  EXPECT_LT(glow->x(), shadow->x());
  EXPECT_LT(glow->y(), shadow->y());
  EXPECT_GE(browser_view->seoul_omnibox_glow_blur_for_testing(), 20.0f);
  EXPECT_LE(browser_view->seoul_omnibox_glow_blur_for_testing(), 250.0f);
  EXPECT_GE(browser_view->seoul_omnibox_glow_opacity_for_testing(), 0.0f);
  EXPECT_LE(browser_view->seoul_omnibox_glow_opacity_for_testing(), 1.0f);

  EXPECT_EQ(427, BrowserView::CalculateSeoulOmniboxWidthForTesting(640));
  EXPECT_EQ(750, BrowserView::CalculateSeoulOmniboxWidthForTesting(1280));

  browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE));
  browser_view->SetBoundsRect(original_bounds);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       UnifiedOmniboxClosesWhileGlowAnimationIsActive) {
  const bool originally_preferred_reduced_motion =
      gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_reduced_motion(base::BindOnce(
      [](bool value) {
        gfx::Animation::SetPrefersReducedMotionForTesting(value);
      },
      originally_preferred_reduced_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(false);
  if (gfx::Animation::PrefersReducedMotion()) {
    GTEST_SKIP() << "A command-line override forces reduced motion";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);

  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());
  ASSERT_TRUE(
      browser_view->seoul_omnibox_search_mode_animation_running_for_testing());

  // Escape closes the surface immediately, while Zen's one-second glow still
  // owns references to the surface and glow layers.
  EXPECT_TRUE(browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(browser_view->seoul_omnibox_action_view_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_backdrop_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_shadow_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_glow_for_testing());
  EXPECT_FALSE(
      browser_view->seoul_omnibox_search_mode_animation_running_for_testing());
  EXPECT_EQ(static_cast<views::View*>(toolbar), location_bar->parent());

  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       UnifiedOmniboxSuppressesGlowForReducedMotion) {
  const bool originally_preferred_reduced_motion =
      gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_reduced_motion(base::BindOnce(
      [](bool value) {
        gfx::Animation::SetPrefersReducedMotionForTesting(value);
      },
      originally_preferred_reduced_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(true);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());
  ASSERT_TRUE(browser_view->seoul_omnibox_shadow_for_testing());
  ASSERT_TRUE(browser_view->seoul_omnibox_glow_for_testing());
  EXPECT_FALSE(
      browser_view->seoul_omnibox_search_mode_animation_running_for_testing());
  EXPECT_EQ(250.0f, browser_view->seoul_omnibox_glow_blur_for_testing());
  EXPECT_EQ(0.0f, browser_view->seoul_omnibox_glow_opacity_for_testing());

  EXPECT_TRUE(browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_shadow_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_glow_for_testing());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       UnifiedOmniboxKeepsZenSelectedForegroundWhite) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);

  browser_view->ShowSeoulOmniboxActions();
  SeoulOmniboxActionView* const actions =
      browser_view->seoul_omnibox_action_view_for_testing();
  ASSERT_TRUE(actions);
  ASSERT_GT(actions->result_count(), 0u);
  views::Label* const mode_label =
      location_bar->seoul_action_mode_label_for_testing();
  ASSERT_TRUE(mode_label);

  const SkColor selected_foreground =
      browser_view->GetColorProvider()->GetColor(
          kColorOmniboxResultsTextSelected);
  EXPECT_EQ(SK_ColorWHITE, selected_foreground);
  EXPECT_EQ(selected_foreground, actions->selected_title_color_for_testing());
  EXPECT_EQ(selected_foreground, mode_label->GetEnabledColor());

  EXPECT_TRUE(browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE)));
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       UnifiedOmniboxActionSurfaceRoutesKeyboardSelection) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::FocusManager* focus_manager = browser_view->GetFocusManager();
  ASSERT_TRUE(focus_manager);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(ui::Accelerator(
      ui::VKEY_K, ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN)));
  auto* actions = browser_view->seoul_omnibox_action_view_for_testing();
  ASSERT_TRUE(actions);
  ASSERT_GE(actions->result_count(), 5u);
  EXPECT_EQ(0u, actions->selected_index_for_testing());

  auto press = [browser_view](ui::KeyboardCode key_code, int flags = 0) {
    return browser_view->HandleSeoulOmniboxActionKeyEvent(
        ui::KeyEvent(ui::EventType::kKeyPressed, key_code,
                     static_cast<ui::EventFlags>(flags)));
  };

  EXPECT_TRUE(press(ui::VKEY_DOWN));
  EXPECT_EQ(1u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_N, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(2u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_P, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(1u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_TAB, ui::EF_SHIFT_DOWN));
  EXPECT_EQ(0u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_NEXT));
  EXPECT_EQ(4u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_PRIOR));
  EXPECT_EQ(0u, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_UP));
  EXPECT_EQ(actions->result_count() - 1, actions->selected_index_for_testing());
  EXPECT_TRUE(press(ui::VKEY_TAB));
  EXPECT_EQ(0u, actions->selected_index_for_testing());

  EXPECT_TRUE(press(ui::VKEY_ESCAPE));
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_FALSE(browser_view->seoul_omnibox_action_view_for_testing());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandLauncherNewTabTransitionsWithoutNtp) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  const int tab_count = browser()->tab_strip_model()->count();

  browser_view->ShowSeoulOmniboxActions();
  auto* const actions = browser_view->seoul_omnibox_action_view_for_testing();
  ASSERT_TRUE(actions);
  ASSERT_GT(actions->result_count(), 0u);
  EXPECT_EQ(0u, actions->selected_index_for_testing());

  EXPECT_TRUE(browser_view->HandleSeoulOmniboxActionKeyEvent(
      ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE)));
  EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing());

  EXPECT_TRUE(browser_view->ShowSeoulNewTabSurface());
  EXPECT_TRUE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_TRUE(browser_view->toolbar()->location_bar_view()->omnibox_view()->
                  HasFocus());
}

// The regression this suite exists for: on a loaded page, click the real
// address field and start typing with no pause at all. Every character has to
// arrive. Seoul expands the docked field into its floating surface when
// editing begins, and that expansion reparents the field, which blurs it;
// restoring focus the way a fresh keyboard invocation does would select the
// text typed so far and let the next keystroke replace it.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       TypingImmediatelyAfterClickingTheOmniboxKeepsEveryKey) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL(
      "/empty.html?a-deliberately-long-committed-url-so-the-resting-field-"
      "elides-and-the-editing-field-does-not");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  browser_view->DeprecatedLayoutImmediately();
  ASSERT_FALSE(browser_view->seoul_omnibox_surface_for_testing())
      << "the field starts docked, which is what makes this a reparent";

  ui::test::EventGenerator generator = MakeEventGenerator();
  generator.MoveMouseTo(omnibox->GetBoundsInScreen().CenterPoint());
  generator.ClickLeftButton();
  ASSERT_TRUE(omnibox->HasFocus());

  // No wait, no run loop, no settling: the first key follows the click.
  constexpr std::string_view kTyped = "seoul immediate typing regression";
  TypeWithRealKeys(generator, kTyped);

  EXPECT_EQ(base::UTF8ToUTF16(kTyped), omnibox->GetText());
  EXPECT_TRUE(omnibox->HasFocus());
  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing())
      << "typing is what floats the field, so it must have floated";
  EXPECT_EQ(browser_view->seoul_omnibox_surface_for_testing(),
            location_bar->parent());
}

// The same invariant for the keyboard entry point, which reaches the floating
// surface through FocusLocation() rather than through the first keystroke.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       TypingImmediatelyAfterFocusShortcutKeepsEveryKey) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/empty.html")));

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);

  ui::test::EventGenerator generator = MakeEventGenerator();
  // Cmd+L is bound to IDC_FOCUS_LOCATION, and the command is what this drives:
  // a raw accelerator does not reach the browser's handler in the headless
  // backend, where the window is never the system key window. FocusLocation()
  // also clears focus when it cannot take it, and activation is asynchronous,
  // so the command has to run against a window that is already active.
  // Everything after the command is real key events.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser()->window()->IsActive();
  })) << "the test window never became active";
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION));
  ASSERT_TRUE(omnibox->HasFocus());
  ASSERT_TRUE(browser_view->seoul_omnibox_surface_for_testing())
      << "the focus shortcut floats the field before any key arrives";

  constexpr std::string_view kTyped = "seoul immediate typing regression";
  TypeWithRealKeys(generator, kTyped);

  EXPECT_EQ(base::UTF8ToUTF16(kTyped), omnibox->GetText());
  EXPECT_TRUE(omnibox->HasFocus());
}

// Presentation is not allowed to interrupt an edit. A lifecycle snapshot or a
// page finishing its load used to be able to dock the surface out from under
// someone typing in it, which cleared focus and stranded the rest of the
// query.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       PresentationCannotDockAnOmniboxBeingTypedInto) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);

  ui::test::EventGenerator generator = MakeEventGenerator();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser()->window()->IsActive();
  })) << "the test window never became active";
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION));
  TypeWithRealKeys(generator, "half typed query");
  ASSERT_EQ(u"half typed query", omnibox->GetText());
  ASSERT_TRUE(browser_view->seoul_omnibox_surface_for_testing());

  // What an arriving snapshot or a completed load does.
  browser_view->SetSeoulOmniboxFloating(false);

  EXPECT_TRUE(browser_view->seoul_omnibox_surface_for_testing())
      << "docking must be refused while the user is editing";
  EXPECT_EQ(u"half typed query", omnibox->GetText());
  EXPECT_TRUE(omnibox->HasFocus());

  // Typing continues into the same field.
  TypeWithRealKeys(generator, " continued");
  EXPECT_EQ(u"half typed query continued", omnibox->GetText());

  // Abandoning the edit releases the protection, so the surface can still be
  // dismissed the moment it stops holding the user's work.
  generator.PressAndReleaseKey(ui::VKEY_ESCAPE);
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
}

// Bug B. A query that names no Seoul command is a search, and the command
// surface must hand it back to Chromium rather than running the command whose
// name happens to share a few letters with it.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandSurfaceSearchesWhatItCannotRun) {
  UseControlledSearchProvider();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  OmniboxViewViews* const omnibox = location_bar->omnibox_view();
  ASSERT_TRUE(omnibox);
  TabStripModel* const tab_strip = browser()->tab_strip_model();

  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());
  ASSERT_TRUE(browser_view->IsSeoulOmniboxShowingActions())
      << "the empty palette lists what it can do";

  ui::test::EventGenerator generator = MakeEventGenerator();
  TypeWithRealKeys(generator, "best mechanical keyboards");

  EXPECT_EQ(u"best mechanical keyboards", omnibox->GetText());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxShowingActions())
      << "no command is named, so the surface must stand aside";
  EXPECT_TRUE(browser_view->IsSeoulOmniboxActionMode())
      << "still the same surface; only the body changed hands";

  generator.PressAndReleaseKey(ui::VKEY_RETURN);

  content::WebContents* const contents = tab_strip->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetVisibleURL().host() == "search.test";
  })) << "Return produced "
      << contents->GetVisibleURL() << " instead of a default-provider search";
  EXPECT_EQ("/find", contents->GetVisibleURL().path());
  EXPECT_EQ("q=best+mechanical+keyboards", contents->GetVisibleURL().query());
}

// The other half of the same rule: a query that does name a command still
// runs it. Both halves have to hold, or the fix for one is a regression in
// the other.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandSurfaceStillRunsACommandThatIsNamed) {
  UseControlledSearchProvider();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ASSERT_TRUE(browser_view->toolbar()->location_bar_view());

  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());

  ui::test::EventGenerator generator = MakeEventGenerator();
  TypeWithRealKeys(generator, "compact");

  ASSERT_TRUE(browser_view->IsSeoulOmniboxShowingActions());
  auto* const actions = browser_view->seoul_omnibox_action_view_for_testing();
  ASSERT_TRUE(actions);
  ASSERT_GT(actions->result_count(), 0u);

  ShellController* const controller =
      service()->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  const bool compact_before = controller->snapshot().compact_mode.enabled;

  generator.PressAndReleaseKey(ui::VKEY_RETURN);

  EXPECT_TRUE(base::test::RunUntil([&] {
    return controller->snapshot().compact_mode.enabled != compact_before;
  })) << "naming a command must still run it";
  EXPECT_EQ(
      GURL(url::kAboutBlankURL),
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL())
      << "and must not also navigate";
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       AssistantCommandOpensAndClosesWithRealKeys) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* panel = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(panel);
  ui::test::EventGenerator generator = MakeEventGenerator();
  for (bool expected_open : {true, false}) {
    browser_view->ShowSeoulOmniboxActions();
    ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());
    TypeWithRealKeys(generator, "assistant");
    ASSERT_TRUE(browser_view->IsSeoulOmniboxShowingActions());
    ASSERT_EQ(
        browser_view->seoul_omnibox_action_view_for_testing()->result_count(),
        1u);
    generator.PressAndReleaseKey(ui::VKEY_RETURN);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return panel->IsSidePanelShowing() == expected_open; }));
    if (expected_open)
      EXPECT_EQ(panel->GetCurrentEntryId(), SidePanelEntryId::kSeoulCanvas);
    EXPECT_FALSE(browser_view->IsSeoulOmniboxActionMode());
    EXPECT_EQ(
        GURL(url::kAboutBlankURL),
        browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL());
  }
}

// A URL typed into the command surface is a navigation, not a search and not
// a command. Chromium's own interpretation decides which, from the text.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandSurfaceNavigatesAnExplicitUrl) {
  UseControlledSearchProvider();
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL target = embedded_test_server()->GetURL("/empty.html");

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  browser_view->ShowSeoulOmniboxActions();
  ASSERT_TRUE(browser_view->IsSeoulOmniboxActionMode());

  ui::test::EventGenerator generator = MakeEventGenerator();
  TypeWithRealKeys(generator, target.spec());
  EXPECT_FALSE(browser_view->IsSeoulOmniboxShowingActions());

  generator.PressAndReleaseKey(ui::VKEY_RETURN);

  content::WebContents* const contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(target, contents->GetLastCommittedURL());
}

// Seoul projects the tab strip; it never replaces it. Adding a tab through the
// normal Chromium path changes the Chromium tab strip's own count, and the
// service remains attached and unbroken.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, TabStripRemainsChromiumOwned) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  const int before = tab_strip->count();
  ASSERT_TRUE(
      AddTabAtIndex(before, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  EXPECT_EQ(tab_strip->count(), before + 1);
  EXPECT_TRUE(service());
}

// The organization model is reachable through the service and bounded; the
// shell reads this model rather than owning tab state. (Default-workspace
// bootstrapping and mutations are covered by the organization unit tests; this
// asserts the browser-level wiring holds.)
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, OrganizationModelIsReachable) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  // EnsureDefaultWorkspace() runs during service construction, so a default
  // workspace always exists.
  EXPECT_GE(svc->model().workspace_count(), 1u);
}

// Regression for the real Projects "+" path. DialogModel aborts the browser
// process if a text field has neither a visible label nor an accessible name,
// so constructing the production dialog here covers the exact invariant that
// a startup-only smoke test cannot exercise.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CreateProjectNameDialogConstructsAndCloses) {
  ASSERT_TRUE(browser()->window());
  views::Widget* dialog = ShowWorkspaceNameDialog(
      browser()->window()->GetNativeWindow(), u"Create project", u"Space name",
      std::u16string(), base::BindOnce([](std::string) {}));
  ASSERT_TRUE(dialog);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(dialog->IsVisible());
  dialog->CloseNow();
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandLauncherIndexesAndActivatesExactLiveTab) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(tab_strip->active_index(), 1);
  const LiveTabKey target = TabKeyAt(0);
  ASSERT_TRUE(target.is_valid());
  base::RunLoop().RunUntilIdle();

  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->shell_service());
  ShellController* controller =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  const std::vector<CommandLauncherEntry> entries =
      controller->CommandLauncherEntries();
  const std::string target_id =
      "tab:" + WindowKey().value() + ":" + target.value();
  const auto indexed =
      std::ranges::find(entries, target_id, &CommandLauncherEntry::id);
  ASSERT_NE(indexed, entries.end());
  EXPECT_EQ(indexed->kind, CommandLauncherEntryKind::kTab);
  EXPECT_EQ(indexed->live_tab, target);

  ASSERT_TRUE(controller->ActivateLiveTab(WindowKey(), target).has_value());
  EXPECT_EQ(tab_strip->active_index(), 0);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CommandLauncherRejectsTabClosedAfterSearch) {
  TabStripModel* tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  const LiveTabKey stale = TabKeyAt(1);
  ASSERT_TRUE(stale.is_valid());
  base::RunLoop().RunUntilIdle();

  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ShellController* controller =
      svc->shell_service()->GetController(WindowKey());
  ASSERT_TRUE(controller);
  const std::vector<CommandLauncherEntry> entries =
      controller->CommandLauncherEntries();
  const std::string stale_id =
      "tab:" + WindowKey().value() + ":" + stale.value();
  ASSERT_NE(std::ranges::find(entries, stale_id, &CommandLauncherEntry::id),
            entries.end());

  tab_strip->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(controller->ActivateLiveTab(WindowKey(), stale).has_value());
  EXPECT_EQ(tab_strip->count(), 1);
}

IN_PROC_BROWSER_TEST_F(
    SeoulShellBrowserTest,
    EssentialCachedFaviconReplacesDefaultAndStaleCallbackIsIgnored) {
  base::RunLoop().RunUntilIdle();
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->shell_service());
  SeoulShellHeaderView* header =
      svc->shell_service()->GetHeaderForTesting(WindowKey());
  ASSERT_TRUE(header);

  ControllableFaviconLookup favicon_lookup;
  header->SetCachedFaviconLookupForTesting(base::BindRepeating(
      &ControllableFaviconLookup::Lookup, base::Unretained(&favicon_lookup)));
  base::ScopedClosureRunner clear_favicon_lookup(base::BindOnce(
      [](SeoulShellHeaderView* header) {
        header->SetCachedFaviconLookupForTesting({});
      },
      base::Unretained(header)));

  const GURL first_url("https://essential-favicon-a.test/");
  auto first = svc->model().CreateOrUpdateEssential(EssentialId(), "Favicon A",
                                                    first_url.spec());
  ASSERT_TRUE(first.has_value());
  base::RunLoop().RunUntilIdle();

  auto stale = std::ranges::find(favicon_lookup.requests, first_url,
                                 &PendingFaviconRequest::page_url);
  ASSERT_NE(stale, favicon_lookup.requests.end());
  ASSERT_TRUE(stale->callback);
  favicon_base::FaviconImageCallback stale_callback =
      std::move(stale->callback);
  EXPECT_TRUE(
      header->EssentialIconForTesting(first.value()).IsImageGenerator());

  // A count change rebuilds the tile tree and starts a fresh lookup for A.
  // The first callback must no longer be able to reach the replacement tile.
  auto second = svc->model().CreateOrUpdateEssential(
      EssentialId(), "Favicon B",
      GURL("https://essential-favicon-b.test/").spec());
  ASSERT_TRUE(second.has_value());
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(
      header->EssentialIconForTesting(first.value()).IsImageGenerator());

  const favicon_base::FaviconImageResult red_favicon =
      SolidFavicon(SK_ColorRED);
  std::move(stale_callback).Run(red_favicon);
  EXPECT_TRUE(
      header->EssentialIconForTesting(first.value()).IsImageGenerator());

  auto current = std::ranges::find_if(
      favicon_lookup.requests,
      [&first_url](const PendingFaviconRequest& request) {
        return request.page_url == first_url && request.callback;
      });
  ASSERT_NE(current, favicon_lookup.requests.end());
  favicon_base::FaviconImageCallback current_callback =
      std::move(current->callback);
  std::move(current_callback).Run(red_favicon);
  EXPECT_TRUE(header->EssentialIconForTesting(first.value()).IsImage());
}

}  // namespace seoul
