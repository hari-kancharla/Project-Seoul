// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_ICON_PICKER_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_ICON_PICKER_H_

#include "seoul/browser/organization/organization_ids.h"

namespace views {
class View;
}

namespace seoul {

class ShellController;

// Shows Zen's searchable emoji/selectable-icon picker anchored to `anchor`.
// A selection is persisted immediately and the picker remains open, matching
// current Zen's Space icon-editing behavior.
void ShowWorkspaceIconPicker(views::View* anchor,
                             ShellController* controller,
                             WorkspaceId workspace_id);

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_ICON_PICKER_H_
