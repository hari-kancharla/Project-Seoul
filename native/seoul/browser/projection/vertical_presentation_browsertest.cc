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
#include "chrome/test/base/in_process_browser_test.h"
#include "components/sessions/core/session_id.h"
#include "content/public/test/browser_test.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
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

  SeoulOrganizationService *service() {
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
  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ProjectionService *projection = svc->projection_service();
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
  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ProjectionService *projection = svc->projection_service();
  ASSERT_TRUE(projection);

  const LiveWindowKey unknown =
      LiveWindowKey::FromSessionId(browser()->session_id().id() + 100000);
  EXPECT_FALSE(projection->GetController(unknown));
  EXPECT_FALSE(projection->GetSwitcher(unknown));
}

} // namespace seoul
