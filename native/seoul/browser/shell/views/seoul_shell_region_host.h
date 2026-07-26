// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_

#include "base/memory/raw_ptr.h"

class VerticalTabStripRegionView;

namespace seoul {

class SeoulShellFooterView;
class SeoulShellHeaderView;
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
  SeoulShellRegionHost(const SeoulShellRegionHost&) = delete;
  SeoulShellRegionHost& operator=(const SeoulShellRegionHost&) = delete;
  ~SeoulShellRegionHost();

  // Attaches header/footer views for `controller` into `region`. Re-attaching
  // to the same region rebinds the controller without duplicating views.
  void Attach(VerticalTabStripRegionView* region, ShellController* controller);
  void SetPresentationCollapsed(bool collapsed);
  // Removes the shell child views from the region. Idempotent.
  void Detach();

  VerticalTabStripRegionView* region() const { return region_; }

 private:
  raw_ptr<VerticalTabStripRegionView> region_ = nullptr;
  raw_ptr<SeoulShellHeaderView> header_ = nullptr;
  raw_ptr<SeoulShellFooterView> footer_ = nullptr;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_REGION_HOST_H_
