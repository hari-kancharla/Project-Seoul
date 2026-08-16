// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_

#include "base/memory/raw_ptr.h"

namespace views {
class View;
}  // namespace views

class BrowserWindowInterface;
class Profile;
class VerticalTabStripRegionView;

namespace seoul {

class SeoulShellFooterView;
class SeoulShellHeaderView;
class SeoulShellSpaceView;
class ShellController;

// Owns the shell header/footer child views attached to one initialized vertical
// tab-strip region. Ownership is scoped to the owning ShellService per-window
// binding (no process-global map). Destruction detaches and removes the shell
// child views from the region, so the host must be destroyed while the region
// is still alive (the integration patch unregisters at the start of
// ResetTabStrip and in the region destructor, before child-view teardown).
class SeoulShellRegionHost {
public:
  SeoulShellRegionHost();
  SeoulShellRegionHost(const SeoulShellRegionHost &) = delete;
  SeoulShellRegionHost &operator=(const SeoulShellRegionHost &) = delete;
  ~SeoulShellRegionHost();

  // Attaches header/footer views for `controller` into `region`. Re-attaching
  // to the same region rebinds the controller without duplicating views.
  void Attach(VerticalTabStripRegionView *region, ShellController *controller,
              BrowserWindowInterface *browser_window, Profile *profile);
  void SetPresentationCollapsed(bool collapsed);
  bool ShowCommandLauncher();
  void SetCommandLauncherVisible(bool visible);
  // Removes the shell child views from the region. Idempotent.
  void Detach();

  VerticalTabStripRegionView *region() const { return region_; }
  SeoulShellHeaderView* header_for_testing() const { return header_; }
  SeoulShellFooterView* footer_for_testing() const { return footer_; }
  SeoulShellSpaceView* space_for_testing() const { return space_; }

private:
  raw_ptr<VerticalTabStripRegionView> region_ = nullptr;
  raw_ptr<SeoulShellHeaderView> header_ = nullptr;
  raw_ptr<SeoulShellSpaceView> space_ = nullptr;
  raw_ptr<views::View> footer_spacer_ = nullptr;
  raw_ptr<SeoulShellFooterView> footer_ = nullptr;
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_
