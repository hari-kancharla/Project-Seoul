// Project Seoul native browser shell V0.

#include "seoul/browser/shell/views/seoul_shell_region_host.h"

#include <memory>
#include <optional>

// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h" // nogncheck
#include "seoul/browser/shell/views/seoul_shell_footer_view.h"
#include "seoul/browser/shell/views/seoul_shell_header_view.h"
#include "seoul/browser/shell/views/seoul_shell_space_view.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace seoul {

SeoulShellRegionHost::SeoulShellRegionHost() = default;

SeoulShellRegionHost::~SeoulShellRegionHost() { Detach(); }

void SeoulShellRegionHost::Attach(VerticalTabStripRegionView *region,
                                  ShellController *controller,
                                  BrowserWindowInterface *browser_window,
                                  Profile *profile) {
  if (!region || !controller || !browser_window || !profile) {
    return;
  }
  region_ = region;
  if (!header_) {
    auto header = std::make_unique<SeoulShellHeaderView>(
        controller, browser_window, profile);
    header_ = region->AddChildView(std::move(header));
  } else {
    header_->BindController(controller);
    header_->BindBrowserContext(browser_window, profile);
  }
  if (!footer_) {
    auto footer = std::make_unique<SeoulShellFooterView>(controller);
    footer_ = region->AddChildView(std::move(footer));
  } else {
    footer_->BindController(controller);
  }
  if (!space_) {
    if (VerticalTabStripView *tab_strip = region->GetSeoulTabStripView()) {
      auto space = std::make_unique<SeoulShellSpaceView>(controller);
      space_ = static_cast<SeoulShellSpaceView *>(
          tab_strip->SetSeoulSpaceIndicator(std::move(space)));
      // Chromium's default vertical strip reserves eight DIPs above its first
      // row. The integrated Seoul toolbar already owns the titlebar spacing,
      // so retain only one compact Zen-style separation here.
      tab_strip->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 8, 0));
    }
  } else {
    space_->BindController(controller);
  }

  views::View *separator = region->GetSeoulShellSeparatorAnchor();
  views::View *bottom = region->GetSeoulShellFooterAnchor();
  if (separator) {
    std::optional<size_t> separator_index = region->GetIndexOf(separator);
    if (separator_index.has_value()) {
      region->ReorderChildView(header_, separator_index.value() + 1);
      if (views::View *tab_strip = region->GetSeoulTabStripView()) {
        region->ReorderChildView(tab_strip, separator_index.value() + 2);
      }
    }
  }
  if (bottom) {
    if (std::optional<size_t> bottom_index = region->GetIndexOf(bottom)) {
      region->ReorderChildView(footer_, bottom_index.value() + 1);
    }
  }
}

void SeoulShellRegionHost::SetPresentationCollapsed(bool collapsed) {
  if (header_) {
    header_->SetPresentationCollapsed(collapsed);
  }
  if (footer_) {
    footer_->SetPresentationCollapsed(collapsed);
  }
  if (space_) {
    space_->SetPresentationCollapsed(collapsed);
  }
}

bool SeoulShellRegionHost::ShowCommandLauncher() {
  return footer_ && footer_->ShowCommandLauncher();
}

void SeoulShellRegionHost::Detach() {
  if (region_ && space_) {
    if (VerticalTabStripView *tab_strip = region_->GetSeoulTabStripView()) {
      tab_strip->ClearSeoulSpaceIndicator();
    }
  }
  space_ = nullptr;
  if (region_ && header_) {
    region_->RemoveChildViewT(header_.get());
  }
  header_ = nullptr;
  if (region_ && footer_) {
    region_->RemoveChildViewT(footer_.get());
  }
  footer_ = nullptr;
  region_ = nullptr;
}

} // namespace seoul
