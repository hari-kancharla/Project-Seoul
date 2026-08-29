// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_handset_size_dialog.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "seoul/browser/handset/viewport_math.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"

namespace seoul {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHandsetWidthFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHandsetHeightFieldId);

class HandsetSizeDelegate : public ui::DialogModelDelegate {
 public:
  explicit HandsetSizeDelegate(base::OnceCallback<void(int, int)> on_accept)
      : on_accept_(std::move(on_accept)) {}

  void OnAccepted() {
    ui::DialogModelTextfield* const width_field =
        dialog_model()->GetTextfieldByUniqueId(kHandsetWidthFieldId);
    ui::DialogModelTextfield* const height_field =
        dialog_model()->GetTextfieldByUniqueId(kHandsetHeightFieldId);
    if (!width_field || !height_field || !on_accept_) {
      return;
    }
    const auto [width, height] =
        ResolveCustomHandsetSize(width_field->text(), height_field->text());
    if (width == 0 && height == 0) {
      // Both blank means the user opened the dialog and changed nothing -
      // leave Handset mode exactly as it was rather than force a resize to
      // the profile default, which would look like the dialog silently
      // overrode a size the user had not touched.
      return;
    }
    std::move(on_accept_).Run(width, height);
  }

 private:
  base::OnceCallback<void(int, int)> on_accept_;
};

}  // namespace

std::unique_ptr<ui::DialogModel> BuildHandsetSizeDialogModel(
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept) {
  auto delegate = std::make_unique<HandsetSizeDelegate>(std::move(on_accept));
  HandsetSizeDelegate* const delegate_ptr = delegate.get();
  return ui::DialogModel::Builder(std::move(delegate))
      .SetTitle(u"Custom Handset size")
      .AddTextfield(kHandsetWidthFieldId, u"Width (dip)",
                    initial_width_dip > 0
                        ? base::NumberToString16(initial_width_dip)
                        : std::u16string())
      .AddTextfield(kHandsetHeightFieldId, u"Height (dip)",
                    initial_height_dip > 0
                        ? base::NumberToString16(initial_height_dip)
                        : std::u16string())
      .AddOkButton(base::BindOnce(&HandsetSizeDelegate::OnAccepted,
                                  base::Unretained(delegate_ptr)),
                  ui::DialogModel::Button::Params().SetLabel(u"Apply"))
      .AddCancelButton(base::DoNothing())
      .Build();
}

views::Widget* ShowHandsetSizeDialog(
    gfx::NativeWindow parent,
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept) {
  return constrained_window::ShowBrowserModal(
      BuildHandsetSizeDialogModel(initial_width_dip, initial_height_dip,
                                  std::move(on_accept)),
      parent);
}

}  // namespace seoul
