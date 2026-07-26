// Project Seoul native browser shell.
// A real modal text-input dialog for creating or renaming a workspace. It
// collects a name from the user and invokes a callback with the entered value;
// there are no fixed or synthesized names. Built on ui::DialogModel and shown
// browser-modal, so it uses the platform dialog with validation and focus
// handling rather than a placeholder.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_NAME_DIALOG_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_NAME_DIALOG_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

namespace views {
class Widget;
}

namespace ui {
class DialogModel;
}

namespace seoul {

// Builds the same production DialogModel used by ShowWorkspaceNameDialog.
// Kept separate so the model and its accessibility invariants can be exercised
// without creating a platform window.
std::unique_ptr<ui::DialogModel> BuildWorkspaceNameDialogModel(
    const std::u16string& title,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept);

// Shows a modal "name a workspace" dialog anchored to `parent`. On accept with
// a non-empty, bounded name, `on_accept` runs with that name; cancel runs
// nothing. `initial_name` prefills the field (empty for create). The returned
// widget is owned by Views and may be used by browser tests to close the modal.
views::Widget* ShowWorkspaceNameDialog(
    gfx::NativeWindow parent,
    const std::u16string& title,
    const std::u16string& initial_name,
    base::OnceCallback<void(std::string)> on_accept);

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_NAME_DIALOG_H_
