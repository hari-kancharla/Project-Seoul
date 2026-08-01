// Project Seoul outbound browser command layer.

#include "seoul/browser/commands/model_command_facade.h"

#include "base/test/bind.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class ModelCommandFacadeTest : public testing::Test {
 protected:
  ModelCommandFacadeTest()
      : model_(base::BindLambdaForTesting([]() { return base::Time(); })),
        facade_(&model_) {
    CHECK(model_.EnsureDefaultWorkspace().has_value());
  }

  OrganizationModel model_;
  ModelCommandFacade facade_;
};

TEST_F(ModelCommandFacadeTest, CreateWorkspace) {
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kCreateWorkspace;
  command.name = "Work";
  EXPECT_TRUE(facade_.Execute(command).has_value());
  EXPECT_EQ(model_.workspace_count(), 2u);
}

TEST_F(ModelCommandFacadeTest, SetWorkspaceIcon) {
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kSetWorkspaceIcon;
  command.workspace_id = model_.default_workspace();
  command.icon = "🌱";
  EXPECT_TRUE(facade_.Execute(command).has_value());
  EXPECT_EQ(model_.FindWorkspace(model_.default_workspace())->icon, "🌱");
}

TEST_F(ModelCommandFacadeTest, RejectsUnsupportedBrowserCommand) {
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kCloseTab;
  EXPECT_EQ(facade_.Execute(command).error(),
            CommandError::kUnsupportedCommand);
}

}  // namespace
}  // namespace seoul
