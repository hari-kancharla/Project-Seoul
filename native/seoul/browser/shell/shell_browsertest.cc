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
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
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
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/image_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

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

  const auto& children = controls->children();
  ASSERT_EQ(3u, children.size());
  EXPECT_EQ(downloads, children[0]);
  EXPECT_EQ(workspaces, children[1]);
  EXPECT_EQ(create_new, children[2]);
  EXPECT_TRUE(downloads->GetVisible());
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_EQ(u"Open Downloads", downloads->GetAccessibleName());
  EXPECT_EQ(u"Workspaces", workspaces->GetAccessibleName());
  EXPECT_EQ(u"Create New", create_new->GetAccessibleName());

  const std::optional<ui::ImageModel>& create_new_icon =
      create_new->GetImageModel(views::Button::STATE_NORMAL);
  ASSERT_TRUE(create_new_icon);
  ASSERT_TRUE(create_new_icon->IsVectorIcon());
  EXPECT_EQ(&vector_icons::kAddIcon,
            create_new_icon->GetVectorIcon().vector_icon());

  // Equal edge controls and a flexible centered workspace control reproduce
  // Zen's expanded `justify-content: space-between` footer.
  EXPECT_EQ(downloads->width(), create_new->width());
  EXPECT_EQ(controls->GetLocalBounds().CenterPoint().x(),
            workspaces->bounds().CenterPoint().x());
  EXPECT_LT(downloads->bounds().CenterPoint().x(),
            workspaces->bounds().CenterPoint().x());
  EXPECT_LT(workspaces->bounds().CenterPoint().x(),
            create_new->bounds().CenterPoint().x());

  footer->SetPresentationCollapsed(true);
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(downloads->GetVisible());
  EXPECT_TRUE(workspaces->GetVisible());
  EXPECT_TRUE(create_new->GetVisible());
  EXPECT_LT(downloads->bounds().CenterPoint().y(),
            workspaces->bounds().CenterPoint().y());
  EXPECT_LT(workspaces->bounds().CenterPoint().y(),
            create_new->bounds().CenterPoint().y());

  footer->SetPresentationCollapsed(false);
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

  // Compact is independent of Appearance. Seed it while Single is active so
  // the durable modes must defer and later restore it.
  controller->SetExpandOnHoverEnabledForWindow(true);
  controller->RequestCollapse(true);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsCollapsed(); }));

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

  controller->SetExpandOnHoverEnabledForWindow(false);
  controller->RequestCollapse(false);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsCollapsed(); }));
  browser_view->SetBoundsRect(original_bounds);
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

  browser_view->SetFocusToLocationBar(/*is_user_initiated=*/false);
  ASSERT_EQ(original_location_bar->omnibox_view(),
            browser_view->GetFocusManager()->GetFocusedView());
  ASSERT_EQ(static_cast<views::View*>(browser_view),
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
  EXPECT_EQ(static_cast<views::View*>(browser_view),
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

  // The address row is intentionally transient and uses the compact 36px rail
  // height while it is docked.
  EXPECT_EQ(36, browser_view->toolbar()->height());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, NewTabUsesChromiumNewTab) {
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL), browser()->GetNewTabURL());
  const int previous_count = browser()->tab_strip_model()->count();
  chrome::NewTab(browser());
  ASSERT_EQ(previous_count + 1, browser()->tab_strip_model()->count());
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL), contents->GetVisibleURL());
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

  EXPECT_EQ(gfx::Rect(32, 83, 576, 62), location_bar->bounds());
  EXPECT_EQ(location_bar->x(), actions->x());
  EXPECT_EQ(location_bar->width(), actions->width());
  EXPECT_EQ(location_bar->bounds().bottom(), actions->y());
  EXPECT_LE(actions->height(), 270);
  EXPECT_EQ(location_bar->bounds().origin(), surface->bounds().origin());
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

  EXPECT_EQ(576, BrowserView::CalculateSeoulOmniboxWidthForTesting(640));
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
