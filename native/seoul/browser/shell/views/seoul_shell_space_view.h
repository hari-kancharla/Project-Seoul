// Copyright 2026 The Project Seoul Authors

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
class LabelButton;
}

namespace seoul {

class ShellController;
class SeoulWorkspaceMenu;

// The active Space indicator that sits between pinned and regular tabs, as it
// does in Zen. Clicking it opens the Space menu.
class SeoulShellSpaceView : public views::View, public ShellObserver {
  METADATA_HEADER(SeoulShellSpaceView, views::View)

 public:
  explicit SeoulShellSpaceView(ShellController* controller);
  SeoulShellSpaceView(const SeoulShellSpaceView&) = delete;
  SeoulShellSpaceView& operator=(const SeoulShellSpaceView&) = delete;
  ~SeoulShellSpaceView() override;

  void BindController(ShellController* controller);
  void SetPresentationCollapsed(bool collapsed);

  void OnShellSnapshotChanged(const ShellChange& change,
                              const ShellSnapshot& snapshot) override;

 private:
  void RebuildFromSnapshot(const ShellSnapshot& snapshot);
  void OnPressed();
  void OnInteractionStateChanged();
  void OnWorkspaceMenuClosed();
  void UpdateInteractionVisuals(bool animate);

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<views::LabelButton> button_ = nullptr;
  raw_ptr<views::ImageButton> menu_button_ = nullptr;
  std::unique_ptr<SeoulWorkspaceMenu> workspace_menu_;
  WorkspaceId rendered_workspace_id_;
  bool presentation_collapsed_ = false;
  bool workspace_menu_open_ = false;
  base::WeakPtrFactory<SeoulShellSpaceView> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_SPACE_VIEW_H_
