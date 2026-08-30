// Project Seoul native browser shell.

#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/third_party/icu/icu_utf.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"

namespace seoul {

namespace {

// The organization model bounds workspace-name length; keep the field within a
// sensible display bound (the model re-validates on the command path).
constexpr size_t kMaxWorkspaceNameLength = 100;

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWorkspaceNameFieldId);

// Owns the accept callback and reads the entered name from the model it is
// attached to. The model owns this delegate, so it outlives dialog dispatch.
class WorkspaceNameDelegate : public ui::DialogModelDelegate {
 public:
  explicit WorkspaceNameDelegate(
      base::OnceCallback<void(std::string)> on_accept)
      : on_accept_(std::move(on_accept)) {}

  void OnAccepted() {
    ui::DialogModelTextfield* field =
        dialog_model()->GetTextfieldByUniqueId(kWorkspaceNameFieldId);
    if (!field || !on_accept_) {
      return;
    }
    std::u16string value = field->text();
    if (value.empty()) {
      return;  // never create/rename to an empty name
    }
    if (value.size() > kMaxWorkspaceNameLength) {
      value.resize(kMaxWorkspaceNameLength);
      // Never cut a surrogate pair in half: a name of emoji trimmed
      // mid-pair ends in a lone surrogate that is invalid UTF-16.
      if (CBU16_IS_LEAD(value.back())) {
        value.pop_back();
      }
    }
    std::move(on_accept_).Run(base::UTF16ToUTF8(value));
  }

 private:
  base::OnceCallback<void(std::string)> on_accept_;
};

}  // namespace

std::unique_ptr<ui::DialogModel> BuildWorkspaceNameDialogModel(
    const std::u16string& title,
    const std::u16string& field_label,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept) {
  auto delegate = std::make_unique<WorkspaceNameDelegate>(std::move(on_accept));
  WorkspaceNameDelegate* delegate_ptr = delegate.get();
  return ui::DialogModel::Builder(std::move(delegate))
      .SetTitle(title)
      // DialogModel requires every text field to have either a visible label
      // or an accessible name. An empty label makes Chromium abort as soon as
      // the create/rename dialog is constructed - and the label names what is
      // being named, which is the caller's to say: a Space here, a Boost in
      // the Boost panel.
      .AddTextfield(kWorkspaceNameFieldId, field_label, initial_name)
      .AddOkButton(base::BindOnce(&WorkspaceNameDelegate::OnAccepted,
                                  base::Unretained(delegate_ptr)))
      .AddCancelButton(base::DoNothing())
      .Build();
}

views::Widget* ShowWorkspaceNameDialog(
    gfx::NativeWindow parent,
    const std::u16string& title,
    const std::u16string& field_label,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept) {
  return constrained_window::ShowBrowserModal(
      BuildWorkspaceNameDialogModel(title, field_label, initial_name,
                                    std::move(on_accept)),
      parent);
}

}  // namespace seoul
