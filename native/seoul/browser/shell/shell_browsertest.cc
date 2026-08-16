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

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
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
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_views.h"
#include "chrome/browser/ui/views/omnibox/omnibox_result_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_row_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
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
#include "chrome/test/base/ui_test_utils.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "seoul/browser/lifecycle/new_tab_placeholder_provenance.h"
#include "seoul/browser/lifecycle/session_restore_metadata.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/shell_service.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_shell_footer_view.h"
#include "seoul/browser/shell/views/seoul_shell_header_view.h"
#include "seoul/browser/shell/views/seoul_shell_space_view.h"
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
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
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

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       DefaultWorkspaceHeaderHasNoSyntheticInitial) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  SeoulShellSpaceView* space =
      svc->shell_service()->GetSpaceForTesting(WindowKey());
  ASSERT_TRUE(space);
  EXPECT_EQ(u"Default", space->text_for_testing());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       SpaceIndicatorPrecedesPinnedTabsAndOwnsCollapseState) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);
  VerticalTabStripView* const tab_strip =
      vertical_region->GetSeoulTabStripView();
  ASSERT_TRUE(tab_strip);
  SeoulShellSpaceView* const space =
      service()->shell_service()->GetSpaceForTesting(WindowKey());
  ASSERT_TRUE(space);

  const views::ProposedLayout layout = tab_strip->CalculateProposedLayout(
      views::SizeBounds(gfx::Size(280, 720)));
  const views::ChildLayout* const indicator_layout = layout.GetLayoutFor(space);
  const views::ChildLayout* const pinned_layout =
      layout.GetLayoutFor(tab_strip->pinned_tabs_scroll_view_for_testing());
  ASSERT_TRUE(indicator_layout);
  ASSERT_TRUE(pinned_layout);
  EXPECT_EQ(indicator_layout->bounds.bottom(), pinned_layout->bounds.y());

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
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  LocationBarView* const location_bar =
      browser_view->toolbar()->location_bar_view();
  ASSERT_TRUE(location_bar);
  ASSERT_TRUE(location_bar->location_icon_view());
  ASSERT_TRUE(location_bar->seoul_floating_search_icon_for_testing());

  // The leading-search treatment is defined for the editing-or-empty omnibox,
  // so the test has to actually be in that state. A fresh test window is NOT:
  // it sits on about:blank, which the omnibox renders as the literal text
  // "about:blank", so the page identity is showing and the search icon is
  // correctly hidden. Typing is the honest way into the editing state, and it
  // is the path a user takes after Cmd+L. An empty string will not do it -
  // OmniboxEditModel treats that as having no input in progress.
  // Update() re-reads the page URL into the omnibox, so it has to run BEFORE
  // the typing, not after - otherwise it reverts exactly the state under test.
  location_bar->Update(browser()->tab_strip_model()->GetActiveWebContents());
  location_bar->GetOmniboxView()->SetUserText(u"seoul");
  browser_view->DeprecatedLayoutImmediately();

  EXPECT_TRUE(location_bar->IsEditingOrEmpty());
  EXPECT_TRUE(
      location_bar->seoul_floating_search_icon_for_testing()->GetVisible());
  EXPECT_FALSE(location_bar->location_icon_view()->GetVisible());

  ASSERT_NE(nullptr,
            ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  location_bar->Revert();
  location_bar->Update(browser()->tab_strip_model()->GetActiveWebContents());
  browser_view->DeprecatedLayoutImmediately();

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

  ShellController* controller = svc->shell_service()->GetController(WindowKey());
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

  // Wraps rather than stopping: scrolling back past the first lands on the last.
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
                       FooterMatchesZenDefaultControlsAndLayout) {
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
  views::View* workspaces = footer->workspaces_control_for_testing();
  views::LabelButton* create_new = footer->create_new_button_for_testing();
  ASSERT_TRUE(controls);
  ASSERT_TRUE(downloads);
  ASSERT_TRUE(workspaces);
  ASSERT_TRUE(create_new);

  // Downloads, the Space strip, Create New.
  //
  // The footer used to carry a sidebar toggle at the leading edge, duplicating
  // the toolbar's at top left - two buttons for one user intent, drawn from
  // different icon sets and driving different state: this one moved Seoul's
  // ShellAppearanceLayoutMode while the toolbar's moved Chromium's
  // vertical-tab compact mode. The toolbar's is the one that remains, and
  // Downloads took this edge, which also keeps the Space strip centred between
  // two real controls rather than a control and a blank spacer.
  const auto& children = controls->children();
  ASSERT_EQ(3u, children.size());
  EXPECT_EQ(downloads, children[0]);
  EXPECT_EQ(workspaces, children[1]);
  EXPECT_EQ(create_new, children[2]);
  EXPECT_TRUE(downloads->GetVisible());
  EXPECT_EQ(u"Downloads", downloads->GetAccessibleName());
  EXPECT_EQ(downloads->GetPreferredSize().width(),
            create_new->GetPreferredSize().width());
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_EQ(u"Workspaces", workspaces->GetAccessibleName());
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

  // A flexible centred workspace control between the edges still reproduces
  // Zen's `justify-content: space-between` footer with one edge control.
  EXPECT_EQ(controls->GetLocalBounds().CenterPoint().x(),
            workspaces->bounds().CenterPoint().x());
  EXPECT_LT(workspaces->bounds().CenterPoint().x(),
            create_new->bounds().CenterPoint().x());

  footer->SetPresentationCollapsed(true);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_LT(workspaces->bounds().CenterPoint().y(),
            create_new->bounds().CenterPoint().y());

  footer->SetPresentationCollapsed(false);
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       CreateNewIconTracksCommandSurfaceLikeZen) {
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
  EXPECT_FALSE(icon->layer()->transform().IsIdentity());

  // Zen's rotated plus is a close affordance, not a one-way status icon.
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

// Expand-on-hover treats focus inside the rail as a reason to hold it open, so
// keyboard tab navigation cannot collapse the strip out from under the user.
// Seoul hosts the toolbar inside that same rail, which makes the two halves of
// this rule pull in opposite directions, and both halves have to hold:
//
//   - toolbar focus must NOT hold the rail open. The omnibox has focus in a new
//     window and after every Cmd+L, so counting it pinned the rail at full
//     width with the mouse nowhere near it and made collapse a silent no-op.
//   - tab-strip focus MUST still hold it open. That is upstream's behavior and
//     narrowing the rule far enough to break it would collapse the rail under a
//     keyboard user mid-navigation.
//
// Nothing else covers the second half, so a future narrowing of the first would
// otherwise go unnoticed.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       ExpandOnHoverSeparatesHostedToolbarFocusFromTabFocus) {
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

  // The omnibox lives in the hosted toolbar, which is inside the rail.
  OmniboxViewViews* const omnibox =
      browser_view->toolbar()->location_bar_view()->omnibox_view();
  ASSERT_TRUE(omnibox);
  omnibox->RequestFocus();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(region->Contains(
      browser_view->GetFocusManager()->GetFocusedView()))
      << "the omnibox must really be inside the rail, or this proves nothing";
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
                       NewTabCommandTogglesZenSurfaceWithoutMutation) {
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

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_NEW_TAB));
  EXPECT_EQ(tab_count, tab_strip->count());
  EXPECT_EQ(source, tab_strip->GetActiveWebContents());
  EXPECT_FALSE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
  EXPECT_FALSE(browser_view->seoul_omnibox_toolbar_placeholder_for_testing());
  EXPECT_FALSE(location_bar->seoul_floating_mode());
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
  EXPECT_FALSE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
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
  EXPECT_FALSE(browser_view->is_seoul_new_tab_surface_pending());
  EXPECT_FALSE(browser_view->seoul_omnibox_surface_for_testing());
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
      browser()->window()->GetNativeWindow(), u"Create project",
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
