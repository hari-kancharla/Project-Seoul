// Project Seoul workspace projection engine V0.
//
// Real in-process browser tests for the projection service against a real
// Browser. They assert the inbound-to-projection wiring holds end to end: a
// real tab-strip change publishes a live snapshot that the projection service
// turns into a per-window controller and switcher keyed to the real window,
// and that unknown windows are never projected. The pure projection/filter
// logic (which tabs hide, fail-open recovery) is covered exhaustively by the
// projection core unit tests. Wired into //chrome/test:browser_tests via the
// integration patch.

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/vertical/tab_collection_node.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/sessions/core/session_id.h"
#include "content/public/test/browser_test.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/projection/vertical_presentation_adapter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace seoul {

class VerticalPresentationBrowserTest : public InProcessBrowserTest {
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
};

// A real tab-strip change flows through the inbound bridge and publishes a live
// snapshot, which the projection service turns into a controller and switcher
// for this real window.
IN_PROC_BROWSER_TEST_F(VerticalPresentationBrowserTest,
                       ProjectionServiceManagesRealWindow) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ProjectionService* projection = svc->projection_service();
  ASSERT_TRUE(projection);

  // Trigger a live snapshot for this window (the startup snapshot predates the
  // projection service's construction, so a fresh tab-strip change is what
  // registers the controller).
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(projection->GetController(WindowKey()));
  EXPECT_TRUE(projection->GetSwitcher(WindowKey()));
}

// The projection service does not fabricate controllers for windows it has
// never observed: an unknown window key yields no controller.
IN_PROC_BROWSER_TEST_F(VerticalPresentationBrowserTest,
                       UnknownWindowIsNotProjected) {
  SeoulOrganizationService* svc = service();
  ASSERT_TRUE(svc);
  ProjectionService* projection = svc->projection_service();
  ASSERT_TRUE(projection);

  const LiveWindowKey unknown =
      LiveWindowKey::FromSessionId(browser()->session_id().id() + 100000);
  EXPECT_FALSE(projection->GetController(unknown));
  EXPECT_FALSE(projection->GetSwitcher(unknown));
}

// Explicitly hidden browser-chrome tabs remain hidden when fail-open disables
// workspace filtering. This exercises the real adapter/node routing so a
// top-level recovery fast path cannot accidentally revive the startup
// placeholder and duplicate "New Tab" in the rail.
IN_PROC_BROWSER_TEST_F(VerticalPresentationBrowserTest,
                       FailOpenDoesNotReviveExplicitlyHiddenTabNode) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  base::RunLoop().RunUntilIdle();

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  VerticalTabStripRegionView* region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(region);
  RootTabCollectionNode* root = region->root_node_for_testing();
  ASSERT_TRUE(root);

  tabs::TabInterface* hidden_tab =
      browser()->tab_strip_model()->GetTabAtIndex(0);
  tabs::TabInterface* ordinary_tab =
      browser()->tab_strip_model()->GetTabAtIndex(1);
  ASSERT_TRUE(hidden_tab);
  ASSERT_TRUE(ordinary_tab);
  TabCollectionNode* hidden_node =
      root->GetNodeForHandle(hidden_tab->GetHandle());
  TabCollectionNode* ordinary_node =
      root->GetNodeForHandle(ordinary_tab->GetHandle());
  ASSERT_TRUE(hidden_node);
  ASSERT_TRUE(ordinary_node);
  ASSERT_TRUE(hidden_node->view());
  ASSERT_TRUE(ordinary_node->view());

  WindowProjection fail_open_projection;
  fail_open_projection.status = ProjectionStatus::kFailOpen;
  fail_open_projection.hidden_tabs.push_back(
      TabStripBridge::KeyForTab(hidden_tab));
  VerticalPresentationAdapter adapter;
  adapter.UpdateProjection(fail_open_projection);
  adapter.ApplyToVerticalTabStripRegion(region);

  EXPECT_FALSE(hidden_node->view()->GetVisible());
  EXPECT_TRUE(ordinary_node->view()->GetVisible());
}

}  // namespace seoul
