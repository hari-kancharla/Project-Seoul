// Project Seoul native browser shell V0.

#include "seoul/browser/shell/views/seoul_workspace_menu.h"

#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/organization_types.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/views/seoul_workspace_icon_picker.h"
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace seoul {

constexpr int kCreateWorkspace = 1000;
constexpr int kRenameWorkspace = 1001;
constexpr int kArchiveWorkspace = 1002;
constexpr int kChangeWorkspaceIcon = 1003;
constexpr int kWorkspaceBase = 2000;

class WorkspaceMenuModel : public ui::SimpleMenuModel,
                           public ui::SimpleMenuModel::Delegate {
 public:
  WorkspaceMenuModel(ShellController* controller,
                     gfx::NativeWindow parent,
                     views::View* anchor,
                     std::vector<WorkspaceId> ids)
      : ui::SimpleMenuModel(this),
        controller_(controller),
        parent_(parent),
        anchor_(anchor),
        workspace_ids_(std::move(ids)) {
    if (!controller_ || !controller_->model()) {
      return;
    }
    const OrganizationSnapshot snap = controller_->model()->ToSnapshot();
    for (const WorkspaceRecord& workspace : snap.workspaces) {
      if (workspace.archived) {
        continue;
      }
      workspace_ids_.push_back(workspace.id);
      AddItem(kWorkspaceBase + static_cast<int>(workspace_ids_.size()) - 1,
              base::UTF8ToUTF16(workspace.name));
    }
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItem(kCreateWorkspace, u"Create Space");
    AddItem(kRenameWorkspace, u"Rename Space");
    AddItem(kChangeWorkspaceIcon, u"Change Space Icon");
    AddItem(kArchiveWorkspace, u"Archive Space");
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    (void)event_flags;
    if (!controller_) {
      return;
    }
    if (command_id >= kWorkspaceBase) {
      const size_t index = static_cast<size_t>(command_id - kWorkspaceBase);
      if (index < workspace_ids_.size()) {
        std::ignore = controller_->SwitchWorkspace(workspace_ids_[index]);
      }
      return;
    }
    switch (command_id) {
      case kCreateWorkspace: {
        // Real name input: create with exactly what the user typed.
        ShellController* controller = controller_;
        ShowWorkspaceNameDialog(
            parent_, u"Create Space", std::u16string(),
            base::BindOnce(
                [](ShellController* controller, std::string name) {
                  BrowserCommand command;
                  command.id = CommandId::Next();
                  command.kind = CommandKind::kCreateWorkspace;
                  command.name = std::move(name);
                  std::ignore =
                      controller->DispatchModelCommand(std::move(command));
                },
                controller));
        return;
      }
      case kRenameWorkspace: {
        ShellController* controller = controller_;
        const WorkspaceId id = controller_->snapshot().workspace.workspace_id;
        ShowWorkspaceNameDialog(
            parent_, u"Rename Space",
            base::UTF8ToUTF16(controller_->snapshot().workspace.name),
            base::BindOnce(
                [](ShellController* controller, WorkspaceId id,
                   std::string name) {
                  BrowserCommand command;
                  command.id = CommandId::Next();
                  command.kind = CommandKind::kRenameWorkspace;
                  command.workspace_id = id;
                  command.name = std::move(name);
                  std::ignore =
                      controller->DispatchModelCommand(std::move(command));
                },
                controller, id));
        return;
      }
      case kChangeWorkspaceIcon:
        ShowWorkspaceIconPicker(anchor_, controller_,
                                controller_->snapshot().workspace.workspace_id);
        return;
      case kArchiveWorkspace: {
        BrowserCommand command;
        command.id = CommandId::Next();
        command.kind = CommandKind::kArchiveWorkspace;
        command.workspace_id = controller_->snapshot().workspace.workspace_id;
        std::ignore = controller_->DispatchModelCommand(std::move(command));
        return;
      }
      default:
        return;
    }
  }

 private:
  raw_ptr<ShellController> controller_;
  gfx::NativeWindow parent_;
  raw_ptr<views::View> anchor_;
  std::vector<WorkspaceId> workspace_ids_;
};

SeoulWorkspaceMenu::SeoulWorkspaceMenu(gfx::NativeWindow parent,
                                       views::View* anchor,
                                       ShellController* controller)
    : parent_(parent), anchor_(anchor), controller_(controller) {}

SeoulWorkspaceMenu::~SeoulWorkspaceMenu() = default;

bool SeoulWorkspaceMenu::Show(base::RepeatingClosure on_menu_closed) {
  if (!anchor_ || !controller_ || !anchor_->GetWidget()) {
    return false;
  }

  // MenuRunner may run asynchronously (and its API explicitly forbids
  // stack-local ownership), so keep both it and the backing model alive for
  // the lifetime of this menu controller.
  model_ = std::make_unique<WorkspaceMenuModel>(controller_, parent_, anchor_,
                                                std::vector<WorkspaceId>());
  menu_runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::HAS_MNEMONICS,
      std::move(on_menu_closed));
  menu_runner_->RunMenuAt(
      anchor_->GetWidget(), nullptr, anchor_->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
  return true;
}

}  // namespace seoul
