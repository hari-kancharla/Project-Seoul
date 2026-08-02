// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "seoul/browser/organization/seoul_organization_service.h"

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/shell/shell_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace seoul {

class SeoulOrganizationServiceBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser() && browser()->window()) {
      browser()->window()->Hide();
      browser()->window()->ShowInactive();
    }
  }

  LiveWindowKey WindowKey() const {
    return LiveWindowKey::FromSessionId(browser()->session_id().id());
  }
};

// Regression coverage for the profile-shutdown dependency graph. Shell
// controllers observe projection controllers and live window state;
// ProjectionService also observes live window state and owns switchers that
// observe CommandExecutor. Shutdown must detach in that order before
// WindowWatcher destroys the live-state provider.
IN_PROC_BROWSER_TEST_F(SeoulOrganizationServiceBrowserTest,
                       ShutdownDetachesDependantsBeforeOwners) {
  SeoulOrganizationService* const service =
      SeoulOrganizationServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(service);
  ASSERT_TRUE(service->shell_service());
  ASSERT_TRUE(service->projection_service());
  ASSERT_TRUE(service->live_window_state_provider());
  ASSERT_TRUE(service->command_executor());

  // Publish a post-construction live snapshot so both the shell and projection
  // layers have per-window controllers attached when shutdown starts.
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(service->shell_service()->GetController(WindowKey()));
  ASSERT_TRUE(service->projection_service()->GetController(WindowKey()));

  // Mirror ProfileKeyedService dependency order. The product runtime observes
  // organization-owned state and therefore shuts down before the organization
  // service.
  SeoulRuntimeService* const runtime =
      SeoulRuntimeServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(runtime);
  runtime->Shutdown();

  service->Shutdown();

  EXPECT_FALSE(service->shell_service());
  EXPECT_FALSE(service->projection_service());
  EXPECT_FALSE(service->live_window_state_provider());
  EXPECT_FALSE(service->command_executor());

  // ProfileKeyedService may defensively invoke Shutdown again during profile
  // destruction. A second call must be a no-op, not a second observer teardown.
  service->Shutdown();
  EXPECT_TRUE(service->model().CreateWorkspace("After shutdown").has_value());
}

}  // namespace seoul
