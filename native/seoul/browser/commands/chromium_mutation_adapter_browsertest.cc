// Project Seoul outbound browser command layer.
//
// Real in-process browser tests for the outbound command path against a real
// Browser, Profile, and TabStripModel. These exercise what a unit test cannot:
// the ChromiumMutationAdapterImpl resolving a Seoul LiveWindowKey back to a
// live Chromium window and performing the actual tab-strip mutation, and the
// profile-scoped CommandExecutor applying a model-only command to the real
// organization model. Wired into //chrome/test:browser_tests via the
// integration patch.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/chromium_mutation_adapter_impl.h"
#include "seoul/browser/commands/command_errors.h"
#include "seoul/browser/commands/command_executor.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/commands/command_types.h"
#include "seoul/browser/commands/target_resolver.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace seoul {

class ChromiumMutationAdapterBrowserTest : public InProcessBrowserTest {
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

  // The Seoul window key the inbound bridge derives for this browser window.
  LiveWindowKey WindowKey() const {
    return LiveWindowKey::FromSessionId(browser()->session_id().id());
  }

  LiveTabKey TabKeyAt(int index) const {
    return TabStripBridge::KeyForTab(
        browser()->tab_strip_model()->GetTabAtIndex(index));
  }
};

// The profile-scoped CommandExecutor applies a model-only command to the real
// organization model. kCreateWorkspace never touches Chromium, so it exercises
// the outbound executor -> model facade -> model path end to end without live
// tab plumbing.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       ExecutorAppliesModelOnlyCommand) {
  SeoulOrganizationService *svc = service();
  ASSERT_TRUE(svc);
  ASSERT_TRUE(svc->command_executor());
  const size_t before = svc->model().workspace_count();

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kCreateWorkspace;
  command.name = "Reading";
  CommandResult<CommandStatus> result =
      svc->command_executor()->Submit(std::move(command));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(svc->model().workspace_count(), before + 1);
}

// The adapter resolves the live window key to the real Browser and inserts a
// real new tab through Chromium's dedicated new-tab path; the tab strip grows
// by one and a valid live tab key is returned.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       OpenNewTabInsertsRealTab) {
  ChromiumMutationAdapterImpl adapter;
  TabStripModel *tab_strip = browser()->tab_strip_model();
  const int before = tab_strip->count();

  LiveTabKey inserted;
  CommandStatusResult result = adapter.OpenNewTab(
      browser()->profile(), ResolvedWindowTarget{WindowKey()},
      CommandForegroundDisposition::kForeground, &inserted);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(tab_strip->count(), before + 1);
  EXPECT_TRUE(inserted.is_valid());
}

// A window key that resolves to no live window is rejected deterministically
// rather than crashing or mutating another window.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       OpenNewTabUnknownWindowRejected) {
  ChromiumMutationAdapterImpl adapter;
  const int before = browser()->tab_strip_model()->count();
  const LiveWindowKey unknown =
      LiveWindowKey::FromSessionId(browser()->session_id().id() + 100000);

  LiveTabKey inserted;
  CommandStatusResult result =
      adapter.OpenNewTab(browser()->profile(), ResolvedWindowTarget{unknown},
                         CommandForegroundDisposition::kForeground, &inserted);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CommandError::kWindowNotFound);
  EXPECT_EQ(browser()->tab_strip_model()->count(), before);
}

// ActivateTab activates the resolved index on the real tab strip.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       ActivateTabActivatesResolvedIndex) {
  ChromiumMutationAdapterImpl adapter;
  TabStripModel *tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_GE(tab_strip->count(), 2);
  ASSERT_NE(tab_strip->active_index(), 0);

  ResolvedTabTarget target;
  target.window = WindowKey();
  target.tab = TabKeyAt(0);
  target.current_index = 0;
  CommandStatusResult result =
      adapter.ActivateTab(browser()->profile(), target);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(tab_strip->active_index(), 0);
}

// SetPinned pins the resolved tab on the real tab strip.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       SetPinnedPinsResolvedTab) {
  ChromiumMutationAdapterImpl adapter;
  TabStripModel *tab_strip = browser()->tab_strip_model();
  ASSERT_FALSE(tab_strip->IsTabPinned(0));

  ResolvedTabTarget target;
  target.window = WindowKey();
  target.tab = TabKeyAt(0);
  target.current_index = 0;
  CommandStatusResult result =
      adapter.SetPinned(browser()->profile(), target, /*pinned=*/true);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(tab_strip->IsTabPinned(0));
}

// MoveTab relocates the resolved tab to the destination index; the same live
// tab is found at the destination afterward.
IN_PROC_BROWSER_TEST_F(ChromiumMutationAdapterBrowserTest,
                       MoveTabMovesResolvedTab) {
  ChromiumMutationAdapterImpl adapter;
  TabStripModel *tab_strip = browser()->tab_strip_model();
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(AddTabAtIndex(2, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_EQ(tab_strip->count(), 3);
  const LiveTabKey moved = TabKeyAt(0);
  ASSERT_TRUE(moved.is_valid());

  ResolvedTabTarget target;
  target.window = WindowKey();
  target.tab = moved;
  target.current_index = 0;
  CommandStatusResult result =
      adapter.MoveTab(browser()->profile(), target, /*destination_index=*/2);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(TabKeyAt(2) == moved);
}

} // namespace seoul
