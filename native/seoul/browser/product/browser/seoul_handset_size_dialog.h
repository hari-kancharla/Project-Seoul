// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
//
// A real modal two-field dialog for typing a custom Handset size. It collects
// a width and a height from the user and invokes a callback with the parsed,
// clamped values; there is no synthesized or fixed size. Built on
// ui::DialogModel, mirroring seoul_workspace_name_dialog.h, so it is the
// platform dialog with real focus handling rather than a placeholder.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

namespace views {
class Widget;
}

namespace ui {
class DialogModel;
}

namespace seoul {

// Builds the same production DialogModel used by ShowHandsetSizeDialog. Kept
// separate so the model can be exercised without creating a platform window.
std::unique_ptr<ui::DialogModel> BuildHandsetSizeDialogModel(
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept);

// Shows a modal "custom Handset size" dialog anchored to `parent`. On accept,
// `on_accept` runs with the two typed values clamped to the supported range
// (see ClampHandsetWidth/ClampHandsetHeight) - a value that fails to parse as
// a positive integer is treated as unset, which resolves to the current
// device's own dimension rather than a jarring default. `initial_width_dip`/
// `initial_height_dip` prefill the fields, letting the dialog reopen showing
// the live size rather than starting blank. The returned widget is owned by
// Views and may be used by browser tests to close the modal.
views::Widget* ShowHandsetSizeDialog(
    gfx::NativeWindow parent,
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_
