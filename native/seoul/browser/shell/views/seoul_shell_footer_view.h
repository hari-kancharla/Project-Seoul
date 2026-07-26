// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/views/view.h"

namespace views {
class BoxLayout;
class Label;
class LabelButton;
} // namespace views

namespace seoul {

class ShellController;

class SeoulShellFooterView : public views::View, public ShellObserver {
  METADATA_HEADER(SeoulShellFooterView, views::View)

public:
  explicit SeoulShellFooterView(ShellController *controller);
  SeoulShellFooterView(const SeoulShellFooterView &) = delete;
  SeoulShellFooterView &operator=(const SeoulShellFooterView &) = delete;
  ~SeoulShellFooterView() override;

  void BindController(ShellController *controller);
  // See SeoulShellHeaderView::SetPresentationCollapsed. A collapsed rail uses
  // Chromium's native new-tab affordance and does not duplicate every product
  // command as an icon.
  void SetPresentationCollapsed(bool collapsed);
  void OnShellSnapshotChanged(const ShellChange &change,
                              const ShellSnapshot &snapshot) override;

private:
  void RebuildFromSnapshot(const ShellSnapshot &snapshot);
  void OnNewTabPressed();
  void OnCanvasPressed();
  void OnBoostPressed();
  void OnCommandLauncherPressed();
  void OnTaskDeckPressed();
  void OnSplitPressed();
  void OnCompactPressed();
  void OnReconcilePressed();

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<views::LabelButton> new_tab_button_ = nullptr;
  raw_ptr<views::LabelButton> canvas_button_ = nullptr;
  raw_ptr<views::LabelButton> boost_button_ = nullptr;
  raw_ptr<views::LabelButton> launcher_button_ = nullptr;
  raw_ptr<views::LabelButton> task_button_ = nullptr;
  raw_ptr<views::LabelButton> split_button_ = nullptr;
  raw_ptr<views::LabelButton> compact_button_ = nullptr;
  raw_ptr<views::LabelButton> reconcile_button_ = nullptr;
  raw_ptr<views::BoxLayout> button_row_layout_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  bool presentation_collapsed_ = false;
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_FOOTER_VIEW_H_
