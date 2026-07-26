// Project Seoul native browser shell.

#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/dialog_model_field.h"

namespace seoul {
namespace {

TEST(SeoulWorkspaceNameDialogTest,
     ProductionCreateProjectFieldHasAccessibleLabel) {
  std::unique_ptr<ui::DialogModel> model = BuildWorkspaceNameDialogModel(
      u"Create project", std::u16string(),
      base::BindOnce([](std::string) {}));
  ASSERT_TRUE(model);

  const auto& fields = model->contents()->fields();
  ASSERT_EQ(fields.size(), 1u);
  ui::DialogModelTextfield* textfield = fields.front()->AsTextfield();
  ASSERT_TRUE(textfield);
  EXPECT_EQ(textfield->label(), u"Project name");
  EXPECT_FALSE(textfield->label().empty());
}

}  // namespace
}  // namespace seoul
