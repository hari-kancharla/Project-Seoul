// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
//
// The model preserves accessible labels and initial values. Native browser
// tests type invalid and valid values into the real dialog, verify the Apply
// state, and check that the resulting dimensions reach the preview page.

#include "seoul/browser/product/browser/seoul_handset_size_dialog.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/dialog_model_field.h"

namespace seoul {
namespace {

TEST(SeoulHandsetSizeDialogTest, FieldsHaveAccessibleLabelsAndPrefill) {
  std::unique_ptr<ui::DialogModel> model = BuildHandsetSizeDialogModel(
      500, 900, base::BindOnce([](int, int) {}));
  ASSERT_TRUE(model);

  const auto& fields = model->contents()->fields();
  ASSERT_EQ(fields.size(), 3u);
  ui::DialogModelTextfield* const width_field = fields[0]->AsTextfield();
  ui::DialogModelTextfield* const height_field = fields[1]->AsTextfield();
  ASSERT_TRUE(width_field);
  ASSERT_TRUE(height_field);
  EXPECT_FALSE(width_field->label().empty());
  EXPECT_FALSE(height_field->label().empty());
  EXPECT_EQ(width_field->text(), u"500");
  EXPECT_EQ(height_field->text(), u"900");
}

TEST(SeoulHandsetSizeDialogTest, ZeroInitialValuesPrefillEmpty) {
  std::unique_ptr<ui::DialogModel> model =
      BuildHandsetSizeDialogModel(0, 0, base::BindOnce([](int, int) {}));
  ASSERT_TRUE(model);
  const auto& fields = model->contents()->fields();
  EXPECT_TRUE(fields[0]->AsTextfield()->text().empty());
  EXPECT_TRUE(fields[1]->AsTextfield()->text().empty());
}

}  // namespace
}  // namespace seoul
