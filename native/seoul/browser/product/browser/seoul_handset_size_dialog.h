// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
//
// A real modal two-field dialog for typing a custom Handset size. It collects
// a width and a height from the user and invokes a callback with validated
// whole-number values; there is no synthesized or fixed size. Built on
// ui::DialogModel, mirroring seoul_workspace_name_dialog.h, so it is the
// platform dialog with real focus handling rather than a placeholder.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_

#include <memory>

#include "base/functional/callback.h"
namespace content {
class WebContents;
}

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
    base::OnceCallback<void(int, int)> on_accept,
    content::WebContents* source = nullptr);

// Shows a browser-modal custom-size dialog for `source`. It closes when the
// source document changes, is destroyed, or its tab is hidden. Apply is enabled
// only for supported integer dimensions. The callback runs after the modal
// closes; callers must revalidate the source before applying a queued action.
// Fields are prefilled from the current size. Views owns the returned widget.
views::Widget* ShowHandsetSizeDialog(
    content::WebContents* source,
    int initial_width_dip,
    int initial_height_dip,
    base::OnceCallback<void(int, int)> on_accept);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_HANDSET_SIZE_DIALOG_H_
