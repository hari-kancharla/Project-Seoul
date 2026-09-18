// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/views/view.h"

namespace ui {
class Event;
class SimpleMenuModel;
}  // namespace ui

namespace views {
class BoxLayout;
class Label;
class LabelButton;
class MenuRunner;
}  // namespace views

namespace seoul {

class SeoulSplitChooserView;
class ShellController;

class SeoulShellFooterView : public views::View, public ShellObserver {
  METADATA_HEADER(SeoulShellFooterView, views::View)

 public:
  explicit SeoulShellFooterView(ShellController* controller);
  SeoulShellFooterView(const SeoulShellFooterView&) = delete;
  SeoulShellFooterView& operator=(const SeoulShellFooterView&) = delete;
  ~SeoulShellFooterView() override;

  void BindController(ShellController* controller);
  // See SeoulShellHeaderView::SetPresentationCollapsed. A collapsed rail uses
  // Chromium's native new-tab affordance and does not duplicate every product
  // command as an icon.
  void SetPresentationCollapsed(bool collapsed);
  void OnShellSnapshotChanged(const ShellChange& change,
                              const ShellSnapshot& snapshot) override;
  // The keyboard command palette is independent of the footer creation menu.
  bool ShowCommandLauncher();
  void SetCommandLauncherVisible(bool visible);

  views::View* controls_row_for_testing() const { return controls_row_; }
  views::View* workspaces_control_for_testing() const {
    return spaces_container_;
  }
  bool first_space_uses_empty_icon_dot_for_testing() const;
  // The Space strip's buttons, in order, so a test can read what the current
  // Space's pill actually says rather than inferring it from the layout.
  const std::vector<raw_ptr<views::LabelButton>>& space_buttons_for_testing()
      const {
    return space_buttons_;
  }
  views::LabelButton* downloads_button_for_testing() const {
    return downloads_button_;
  }
  views::LabelButton* assistant_button_for_testing() const {
    return assistant_button_;
  }

  views::LabelButton* create_new_button_for_testing() const {
    return create_new_button_;
  }
  views::View* create_new_icon_for_testing() const;
  bool is_command_launcher_visible_for_testing() const {
    return command_launcher_visible_;
  }
  bool is_create_menu_running_for_testing() const;

 private:
  void RebuildFromSnapshot(const ShellSnapshot& snapshot);
  bool CanUpdateSpaceButtons(const ShellSnapshot& snapshot) const;
  void RebuildSpaceButtons(const ShellSnapshot& snapshot);
  void UpdateSpaceButtons(const ShellSnapshot& snapshot, bool animate);
  void OnSpaceScrollSwitch(int direction);
  void OnSpacePressed(WorkspaceId workspace_id);
  void OnDownloadsPressed();
  void OnAssistantPressed();
  void OnCreateNewPressed(const ui::Event& event);
  void OnCreateActionSelected(ShellUtilityAction action);
  void OnCreateMenuClosed();
  void ShowSplitChooser();
  void OnReconcilePressed();

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<views::View> controls_row_ = nullptr;
  raw_ptr<views::BoxLayout> controls_layout_ = nullptr;
  raw_ptr<views::LabelButton> downloads_button_ = nullptr;
  raw_ptr<views::LabelButton> assistant_button_ = nullptr;
  raw_ptr<views::View> spaces_container_ = nullptr;
  raw_ptr<views::BoxLayout> spaces_layout_ = nullptr;
  raw_ptr<views::LabelButton> create_new_button_ = nullptr;
  raw_ptr<views::LabelButton> reconcile_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  std::vector<raw_ptr<views::LabelButton>> space_buttons_;
  std::unique_ptr<SeoulSplitChooserView> split_chooser_;
  // The runner references the model and must be destroyed first.
  std::unique_ptr<ui::SimpleMenuModel> create_menu_model_;
  std::unique_ptr<views::MenuRunner> create_menu_runner_;
  std::optional<ShellUtilityAction> pending_create_action_;
  std::vector<ShellSpaceItem> rendered_spaces_;
  bool presentation_collapsed_ = false;
  bool command_launcher_visible_ = false;
  base::WeakPtrFactory<SeoulShellFooterView> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_
