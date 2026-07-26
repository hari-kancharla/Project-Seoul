// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/view.h"

namespace ui {
class SimpleMenuModel;
}

namespace views {
class BoxLayout;
class Label;
class LabelButton;
class MenuRunner;
} // namespace views

namespace seoul {

class ShellController;

class SeoulShellHeaderView : public views::View, public ShellObserver {
  METADATA_HEADER(SeoulShellHeaderView, views::View)

public:
  explicit SeoulShellHeaderView(ShellController *controller);
  SeoulShellHeaderView(const SeoulShellHeaderView &) = delete;
  SeoulShellHeaderView &operator=(const SeoulShellHeaderView &) = delete;
  ~SeoulShellHeaderView() override;

  void BindController(ShellController *controller);
  // Changes only the way the shell is presented. The durable expanded/
  // collapsed mode remains owned by ShellController; compact-mode hover uses
  // this to reveal the same full hierarchy without mutating product state.
  void SetPresentationCollapsed(bool collapsed);
  void OnShellSnapshotChanged(const ShellChange &change,
                              const ShellSnapshot &snapshot) override;
  bool AcceleratorPressed(const ui::Accelerator &accelerator) override;

private:
  void RebuildFromSnapshot(const ShellSnapshot &snapshot);
  void OnWorkspaceButtonPressed();
  void OnCommandLauncherPressed();
  void OnEssentialsOverflowPressed();

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<views::View> workspace_row_ = nullptr;
  raw_ptr<views::LabelButton> workspace_button_ = nullptr;
  raw_ptr<views::LabelButton> launcher_button_ = nullptr;
  raw_ptr<views::Label> essentials_label_ = nullptr;
  raw_ptr<views::View> essentials_container_ = nullptr;
  raw_ptr<views::BoxLayout> essentials_layout_ = nullptr;
  raw_ptr<views::LabelButton> essentials_overflow_button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> essentials_overflow_menu_model_;
  std::unique_ptr<views::MenuRunner> essentials_overflow_menu_runner_;
  std::vector<ShellEssentialItem> rendered_essentials_;
  std::vector<ShellEssentialItem> overflow_essentials_;
  bool essentials_initialized_ = false;
  bool essentials_collapsed_ = false;
  bool presentation_collapsed_ = false;
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_
