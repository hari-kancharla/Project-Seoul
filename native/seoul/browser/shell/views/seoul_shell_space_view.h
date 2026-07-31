// Copyright 2026 The Project Seoul Authors

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
class Label;
class Textfield;
}  // namespace views

namespace seoul {

class ShellController;
class SeoulWorkspaceMenu;

// The active Space indicator that sits above the current Space's pinned and
// regular tabs, as it does in Zen. The trailing actions button owns the Space
// menu. As in current Zen, the icon and name have distinct double-click
// behavior when no pinned tabs occupy the current Space.
class SeoulShellSpaceView : public views::View,
                            public ShellObserver,
                            public views::TextfieldController {
  METADATA_HEADER(SeoulShellSpaceView, views::View)

 public:
  explicit SeoulShellSpaceView(ShellController* controller);
  SeoulShellSpaceView(const SeoulShellSpaceView&) = delete;
  SeoulShellSpaceView& operator=(const SeoulShellSpaceView&) = delete;
  ~SeoulShellSpaceView() override;

  void BindController(ShellController* controller);
  void SetPresentationCollapsed(bool collapsed);
  void SetPinnedCollapsedChangedCallback(
      base::RepeatingCallback<void(bool)> callback);
  std::u16string text_for_testing() const;
  bool pinned_collapsed_for_testing() const { return pinned_collapsed_; }

  void OnShellSnapshotChanged(const ShellChange& change,
                              const ShellSnapshot& snapshot) override;

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void RebuildFromSnapshot(const ShellSnapshot& snapshot);
  void OnMenuPressed();
  void OnIndicatorPressed();
  void OnIndicatorIconDoubleClicked();
  void OnIndicatorNameDoubleClicked();
  void OnIndicatorHoverChanged(bool hovered);
  void OnInteractionStateChanged();
  void OnWorkspaceMenuClosed();
  void BeginRename();
  void FinishRename(bool commit);
  void UpdateInteractionVisuals(bool animate);

  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<views::View> indicator_glyph_ = nullptr;
  raw_ptr<views::Label> indicator_name_label_ = nullptr;
  raw_ptr<views::Textfield> rename_field_ = nullptr;
  raw_ptr<views::ImageButton> menu_button_ = nullptr;
  std::unique_ptr<SeoulWorkspaceMenu> workspace_menu_;
  WorkspaceId rendered_workspace_id_;
  bool presentation_collapsed_ = false;
  bool indicator_hovered_ = false;
  bool workspace_menu_open_ = false;
  bool renaming_ = false;
  bool pinned_collapsed_ = false;
  size_t rendered_pinned_count_ = 0;
  std::map<WorkspaceId, bool> pinned_collapsed_by_workspace_;
  base::RepeatingCallback<void(bool)> pinned_collapsed_changed_callback_;
  base::WeakPtrFactory<SeoulShellSpaceView> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_
