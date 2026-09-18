// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
#include "seoul/browser/product/browser/seoul_site_controls_menu.h"

#include <utility>
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/handset_mode.h"
#include "seoul/browser/product/browser/seoul_shields_bubble.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {
constexpr int kBoost = 1;
constexpr int kPhone = 2;
constexpr int kShields = 3;
constexpr int kPageInfo = 4;
}  // namespace
SiteControlsMenu::SiteControlsMenu() = default;
SiteControlsMenu::~SiteControlsMenu() {
  weak_factory_.InvalidateWeakPtrs();
  Observe(nullptr);
  runner_.reset();
}
void SiteControlsMenu::Show(views::View* anchor,
                            content::WebContents* contents,
                            const ui::Event& event) {
  if (!anchor || !anchor->GetWidget() || !contents ||
      !contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS())
    return;
  if (runner_ && runner_->IsRunning()) {
    Cancel();
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  runner_.reset();
  pending_.reset();
  Observe(contents);
  page_ = contents->GetLastCommittedURL();
  anchor_.SetView(anchor);
  model_ = std::make_unique<ui::SimpleMenuModel>(this);
  model_->AddItemWithIcon(kBoost, u"Boost this site…",
                          ui::ImageModel::FromVectorIcon(
                              kSeoulBoostIcon, kColorToolbarButtonIcon, 16));
  if (CanHandsetWebContents(contents)) {
    model_->AddItemWithIcon(
        kPhone,
        IsHandsetModeEnabled(contents) ? u"Change phone view…" : u"Phone view…",
        ui::ImageModel::FromVectorIcon(kSeoulHandsetIcon,
                                       kColorToolbarButtonIcon, 16));
  }
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddItemWithIcon(
      kShields, u"Shields for this site…",
      ui::ImageModel::FromVectorIcon(vector_icons::kShieldIcon,
                                     kColorToolbarButtonIcon, 16));
  model_->AddItemWithIcon(
      kPageInfo, u"Site information…",
      ui::ImageModel::FromVectorIcon(vector_icons::kInfoOutlineIcon,
                                     kColorToolbarButtonIcon, 16));
  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&SiteControlsMenu::Closed,
                          weak_factory_.GetWeakPtr()));
  anchor->GetViewAccessibility().SetIsExpanded();
  auto alive = weak_factory_.GetWeakPtr();
  runner_->RunMenuAt(anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopRight,
                     event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                                        : ui::mojom::MenuSourceType::kMouse);
  if (alive && !runner_->IsRunning())
    Closed();
}
void SiteControlsMenu::ExecuteCommand(int command_id, int event_flags) {
  pending_ = command_id;
}
void SiteControlsMenu::Closed() {
  if (anchor_.view())
    anchor_.view()->GetViewAccessibility().SetIsCollapsed();
  auto command = std::exchange(pending_, std::nullopt);
  if (command) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SiteControlsMenu::Run,
                                  weak_factory_.GetWeakPtr(), *command));
  }
}
void SiteControlsMenu::Run(int command) {
  auto* contents = web_contents();
  auto* anchor = anchor_.view();
  auto* browser = contents ? chrome::FindBrowserWithTab(contents) : nullptr;
  if (!contents || !anchor || !anchor->IsDrawn() || !browser ||
      contents->GetLastCommittedURL() != page_ ||
      browser->tab_strip_model()->GetActiveWebContents() != contents)
    return;
  switch (command) {
    case kBoost:
      OpenBoostEditorForWebContents(contents);
      break;
    case kPhone:
      handset_.Show(anchor, contents);
      break;
    case kShields:
      ShowShieldsBubbleForWebContents(contents);
      break;
    case kPageInfo:
      if (auto* view = BrowserView::GetBrowserViewForBrowser(browser))
        view->GetLocationBarView()->ShowPageInfoDialog();
      break;
  }
}
void SiteControlsMenu::Cancel() {
  pending_.reset();
  weak_factory_.InvalidateWeakPtrs();
  if (runner_ && runner_->IsRunning())
    runner_->Cancel();
  if (anchor_.view())
    anchor_.view()->GetViewAccessibility().SetIsCollapsed();
}
void SiteControlsMenu::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame())
    Cancel();
}
void SiteControlsMenu::WebContentsDestroyed() {
  Cancel();
  Observe(nullptr);
}
}  // namespace seoul
