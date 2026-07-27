// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_MENU_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_MENU_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/view.h"

namespace views {
class MenuRunner;
}

namespace seoul {

class ShellController;
class WorkspaceMenuModel;

class SeoulWorkspaceMenu {
 public:
  SeoulWorkspaceMenu(gfx::NativeWindow parent,
                     views::View* anchor,
                     ShellController* controller);
  SeoulWorkspaceMenu(const SeoulWorkspaceMenu&) = delete;
  SeoulWorkspaceMenu& operator=(const SeoulWorkspaceMenu&) = delete;
  ~SeoulWorkspaceMenu();

  // Returns false when the menu cannot be anchored. `on_menu_closed` is
  // invoked after a successfully shown menu closes so its owner can keep its
  // open-state affordance synchronized with the native MenuRunner.
  bool Show(base::RepeatingClosure on_menu_closed = base::RepeatingClosure());

 private:
  gfx::NativeWindow parent_;
  raw_ptr<views::View> anchor_ = nullptr;
  raw_ptr<ShellController> controller_ = nullptr;
  std::unique_ptr<WorkspaceMenuModel> model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_WORKSPACE_MENU_H_
