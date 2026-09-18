// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_handset_size_dialog.h"

#include <optional>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/handset/viewport_math.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/dialog_model_host.h"

namespace seoul {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHandsetWidthFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHandsetHeightFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHandsetApplyButtonId);

class HandsetSizeDelegate : public ui::DialogModelDelegate,
                            public content::WebContentsObserver {
 public:
  HandsetSizeDelegate(base::OnceCallback<void(int, int)> on_accept,
                      content::WebContents* source)
      : content::WebContentsObserver(source),
        on_accept_(std::move(on_accept)) {}

  void PrimaryPageChanged(content::Page&) override { CloseForSourceChange(); }
  void WebContentsDestroyed() override { CloseForSourceChange(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN)
      CloseForSourceChange();
  }

  void ObserveFields() {
    const auto changed = base::BindRepeating(
        &HandsetSizeDelegate::UpdateValidity, base::Unretained(this));
    width_changed_ = dialog_model()
                         ->GetTextfieldByUniqueId(kHandsetWidthFieldId)
                         ->AddOnFieldChangedCallback(changed);
    height_changed_ = dialog_model()
                          ->GetTextfieldByUniqueId(kHandsetHeightFieldId)
                          ->AddOnFieldChangedCallback(changed);
    UpdateValidity();
  }

  bool OnAccepted() {
    const auto size = ValidSize();
    if (!size || !on_accept_)
      return false;
    // Release the modal dialog before creating or activating a preview tab.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(on_accept_), size->first, size->second));
    return true;
  }

 private:
  void CloseForSourceChange() {
    on_accept_.Reset();
    if (dialog_model() && dialog_model()->host())
      dialog_model()->host()->Close();  // Destroys this delegate.
  }

  std::optional<std::pair<int, int>> ValidSize() {
    int width = 0, height = 0;
    if (!base::StringToInt(dialog_model()
                               ->GetTextfieldByUniqueId(kHandsetWidthFieldId)
                               ->text(),
                           &width) ||
        !base::StringToInt(dialog_model()
                               ->GetTextfieldByUniqueId(kHandsetHeightFieldId)
                               ->text(),
                           &height) ||
        width < kMinHandsetWidthDip || width > kMaxHandsetWidthDip ||
        height < kMinHandsetHeightDip || height > kMaxHandsetHeightDip)
      return std::nullopt;
    return std::pair(width, height);
  }
  void UpdateValidity() {
    dialog_model()->SetButtonEnabled(
        dialog_model()->GetButtonByUniqueId(kHandsetApplyButtonId),
        ValidSize().has_value());
  }
  base::OnceCallback<void(int, int)> on_accept_;
  base::CallbackListSubscription width_changed_;
  base::CallbackListSubscription height_changed_;
};

}  // namespace

std::unique_ptr<ui::DialogModel> BuildHandsetSizeDialogModel(
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept,
    content::WebContents* source) {
  auto delegate =
      std::make_unique<HandsetSizeDelegate>(std::move(on_accept), source);
  HandsetSizeDelegate* const delegate_ptr = delegate.get();
  auto model =
      ui::DialogModel::Builder(std::move(delegate))
          .SetTitle(u"Custom phone size")
          .AddTextfield(kHandsetWidthFieldId, u"Width (CSS pixels)",
                        initial_width_dip > 0
                            ? base::NumberToString16(initial_width_dip)
                            : std::u16string())
          .AddTextfield(kHandsetHeightFieldId, u"Height (CSS pixels)",
                        initial_height_dip > 0
                            ? base::NumberToString16(initial_height_dip)
                            : std::u16string())
          .AddParagraph(
              ui::DialogModelLabel(u"Enter whole numbers: width 240–1366 and "
                                   u"height 320–1600 CSS pixels."))
          .AddOkButton(
              base::BindRepeating(&HandsetSizeDelegate::OnAccepted,
                                  base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params().SetLabel(u"Apply").SetId(
                  kHandsetApplyButtonId))
          .AddCancelButton(base::DoNothing())
          .Build();
  delegate_ptr->ObserveFields();
  return model;
}

views::Widget* ShowHandsetSizeDialog(
    content::WebContents* source,
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept) {
  if (!source)
    return nullptr;
  return constrained_window::ShowBrowserModal(
      BuildHandsetSizeDialogModel(initial_width_dip, initial_height_dip,
                                  std::move(on_accept), source),
      source->GetTopLevelNativeWindow());
}

}  // namespace seoul
