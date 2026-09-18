// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SITE_CONTROLS_MENU_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SITE_CONTROLS_MENU_H_

#include <memory>
#include <optional>
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/product/browser/handset_picker_menu.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view_tracker.h"
#include "url/gurl.h"

namespace ui {
class Event;
}
namespace views {
class MenuRunner;
}
namespace seoul {

// Owns the menu until native tracking has ended. Actions run after focus and
// capture are released, and only against the page that opened the menu.
class SiteControlsMenu : public ui::SimpleMenuModel::Delegate,
                         public content::WebContentsObserver {
 public:
  SiteControlsMenu();
  ~SiteControlsMenu() override;
  void Show(views::View* anchor,
            content::WebContents* contents,
            const ui::Event& event);
  void ExecuteCommand(int command_id, int event_flags) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;

 private:
  void Cancel();
  void Closed();
  void Run(int command);
  views::ViewTracker anchor_;
  GURL page_;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<views::MenuRunner> runner_;
  HandsetPickerMenu handset_;
  std::optional<int> pending_;
  base::WeakPtrFactory<SiteControlsMenu> weak_factory_{this};
};
}  // namespace seoul
#endif
