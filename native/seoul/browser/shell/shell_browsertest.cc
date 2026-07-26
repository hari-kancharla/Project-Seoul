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

#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/shell_service.h"
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace seoul {

class SeoulShellBrowserTest : public InProcessBrowserTest {
protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser() && browser()->window()) {
      browser()->window()->Hide();
      browser()->window()->ShowInactive();
    }
  }

  SeoulOrganizationService *service() {
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
  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  EXPECT_TRUE(svc->projection_service());
  EXPECT_TRUE(svc->shell_service());
  EXPECT_TRUE(svc->command_executor());
  EXPECT_TRUE(svc->lifecycle_coordinator());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest,
                       FreshWindowSettlesWithoutFailOpenBanner) {
  base::RunLoop().RunUntilIdle();
  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ShellController *controller =
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

  auto *controller = tabs::VerticalTabStripStateController::From(browser());
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
                       ContentsDoNotPaintUnderVerticalRail) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* vertical_region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(vertical_region);
  ASSERT_TRUE(browser_view->contents_container());
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  const gfx::Rect rail = vertical_region->bounds();
  const gfx::Rect contents = browser_view->contents_container()->bounds();
  EXPECT_GE(contents.x(), rail.right());
  EXPECT_FALSE(vertical_region->GetTopContainer()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, NewTabOpensSeoulCanvas) {
  EXPECT_EQ(GURL("chrome://seoul-canvas/"), browser()->GetNewTabURL());
  const int previous_count = browser()->tab_strip_model()->count();
  chrome::NewTab(browser());
  ASSERT_EQ(previous_count + 1, browser()->tab_strip_model()->count());
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(GURL("chrome://seoul-canvas/"), contents->GetLastCommittedURL());
}

// Seoul projects the tab strip; it never replaces it. Adding a tab through the
// normal Chromium path changes the Chromium tab strip's own count, and the
// service remains attached and unbroken.
IN_PROC_BROWSER_TEST_F(SeoulShellBrowserTest, TabStripRemainsChromiumOwned) {
  TabStripModel *tab_strip = browser()->tab_strip_model();
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
  SeoulOrganizationService *svc = service();
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
  TabStripModel *tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(tab_strip->active_index(), 1);
  const LiveTabKey target = TabKeyAt(0);
  ASSERT_TRUE(target.is_valid());
  base::RunLoop().RunUntilIdle();

  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->shell_service());
  ShellController *controller =
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
  TabStripModel *tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  const LiveTabKey stale = TabKeyAt(1);
  ASSERT_TRUE(stale.is_valid());
  base::RunLoop().RunUntilIdle();

  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ShellController *controller =
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

} // namespace seoul
