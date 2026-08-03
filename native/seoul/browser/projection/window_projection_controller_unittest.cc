// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license.

#include "seoul/browser/projection/window_projection_controller.h"

#include "seoul/browser/organization/organization_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

constexpr int kWindowSessionId = 1;
constexpr int kTabSessionId = 10;

LiveWindowTabState ActiveTabState(bool is_placeholder) {
  LiveWindowTabState live;
  live.window = LiveWindowKey::FromSessionId(kWindowSessionId);
  live.active_tab = LiveTabKey::FromSessionId(kTabSessionId);
  LiveTabDescriptor tab;
  tab.tab = live.active_tab;
  tab.strip_order = 0;
  tab.is_active = true;
  tab.is_new_tab_placeholder = is_placeholder;
  live.tabs.push_back(tab);
  return live;
}

void InitializeModel(OrganizationModel* model) {
  ASSERT_TRUE(model);
  EXPECT_TRUE(model->EnsureDefaultWorkspace().has_value());
  EXPECT_TRUE(model
                  ->SetActiveWorkspaceForWindow(
                      LiveWindowKey::FromSessionId(kWindowSessionId).value(),
                      model->default_workspace())
                  .has_value());
}

TEST(WindowProjectionControllerTest, ActivePlaceholderDoesNotEnterFailOpen) {
  OrganizationModel model;
  InitializeModel(&model);
  WindowProjectionController controller(
      LiveWindowKey::FromSessionId(kWindowSessionId), &model,
      /*lifecycle=*/nullptr);

  controller.UpdateLiveState(ActiveTabState(/*is_placeholder=*/true));

  EXPECT_FALSE(controller.fail_open());
  EXPECT_EQ(controller.projection().status, ProjectionStatus::kEmptyWorkspace);
  EXPECT_TRUE(controller.projection().tabs.empty());
  ASSERT_EQ(controller.projection().hidden_tabs.size(), 1u);
  EXPECT_EQ(controller.projection().hidden_tabs.front(),
            LiveTabKey::FromSessionId(kTabSessionId));
}

TEST(WindowProjectionControllerTest, ActivePlaceholderClearsPriorFailOpen) {
  OrganizationModel model;
  InitializeModel(&model);
  WindowProjectionController controller(
      LiveWindowKey::FromSessionId(kWindowSessionId), &model,
      /*lifecycle=*/nullptr);

  controller.UpdateLiveState(ActiveTabState(/*is_placeholder=*/false));
  ASSERT_TRUE(controller.fail_open());
  ASSERT_EQ(controller.projection().status, ProjectionStatus::kFailOpen);

  controller.UpdateLiveState(ActiveTabState(/*is_placeholder=*/true));

  EXPECT_FALSE(controller.fail_open());
  EXPECT_EQ(controller.projection().status, ProjectionStatus::kEmptyWorkspace);
  EXPECT_TRUE(controller.projection().tabs.empty());
}

}  // namespace
}  // namespace seoul
