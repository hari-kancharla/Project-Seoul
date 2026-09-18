// Project Seoul native browser shell.

#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"

#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "seoul/browser/organization/organization_limits.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"

namespace seoul {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWorkspaceNameFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWorkspaceNameAcceptId);

// Owns the accept callback and reads the entered name from the model it is
// attached to. The model owns this delegate, so it outlives dialog dispatch.
class WorkspaceNameDelegate : public ui::DialogModelDelegate {
 public:
  explicit WorkspaceNameDelegate(
      base::OnceCallback<void(std::string)> on_accept)
      : on_accept_(std::move(on_accept)) {}

  void Initialize() {
    auto* field = dialog_model()->GetTextfieldByUniqueId(kWorkspaceNameFieldId);
    text_subscription_ = field->AddOnFieldChangedCallback(base::BindRepeating(
        &WorkspaceNameDelegate::UpdateButton, base::Unretained(this)));
    UpdateButton();
  }

  bool OnAccepted() {
    const auto value = ValidatedName();
    if (value.empty() || !on_accept_) {
      return false;
    }
    std::move(on_accept_).Run(value);
    return true;
  }

 private:
  std::string ValidatedName() {
    const auto* field =
        dialog_model()->GetTextfieldByUniqueId(kWorkspaceNameFieldId);
    std::u16string name;
    base::TrimWhitespace(field->text(), base::TRIM_ALL, &name);
    const auto value = base::UTF16ToUTF8(name);
    return value.size() <= kMaxNameLength ? value : std::string();
  }

  void UpdateButton() {
    dialog_model()->SetButtonEnabled(
        dialog_model()->GetButtonByUniqueId(kWorkspaceNameAcceptId),
        !ValidatedName().empty());
  }

  base::OnceCallback<void(std::string)> on_accept_;
  base::CallbackListSubscription text_subscription_;
};

}  // namespace

std::unique_ptr<ui::DialogModel> BuildWorkspaceNameDialogModel(
    const std::u16string& title,
    const std::u16string& field_label,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept,
    const std::u16string& description) {
  auto delegate = std::make_unique<WorkspaceNameDelegate>(std::move(on_accept));
  WorkspaceNameDelegate* delegate_ptr = delegate.get();
  auto builder = ui::DialogModel::Builder(std::move(delegate));
  if (!description.empty()) {
    builder.AddParagraph(ui::DialogModelLabel(description));
  }
  auto model =
      builder
          .SetTitle(title)
          // DialogModel requires every text field to have either a visible
          // label or an accessible name. An empty label makes Chromium abort as
          // soon as the create/rename dialog is constructed - and the label
          // names what is being named, which is the caller's to say: a Space
          // here, a Boost in the Boost panel.
          .AddTextfield(kWorkspaceNameFieldId, field_label, initial_name)
          .SetInitiallyFocusedField(kWorkspaceNameFieldId)
          .AddOkButton(
              base::BindRepeating(&WorkspaceNameDelegate::OnAccepted,
                                  base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params()
                  .SetId(kWorkspaceNameAcceptId)
                  .SetLabel(initial_name.empty() ? u"Create" : u"Save"))
          .AddCancelButton(base::DoNothing())
          .Build();
  delegate_ptr->Initialize();
  return model;
}

views::Widget* ShowWorkspaceNameDialog(
    gfx::NativeWindow parent,
    const std::u16string& title,
    const std::u16string& field_label,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept,
    const std::u16string& description) {
  return constrained_window::ShowBrowserModal(
      BuildWorkspaceNameDialogModel(title, field_label, initial_name,
                                    std::move(on_accept), description),
      parent);
}

}  // namespace seoul
