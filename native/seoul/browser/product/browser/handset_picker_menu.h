// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

namespace content {
class WebContents;
}
namespace views {
class MenuRunner;
class View;
}

namespace seoul {

// Command ids past the profile range, which is assigned starting at 0 - one
// per catalogue entry in HandsetProfiles() order - so these have to sit above
// however many profiles the catalogue happens to hold. Public so a test can
// drive ExecuteCommand without duplicating these as magic numbers.
inline constexpr int kHandsetPickerCommandCustomSize = 1000;
inline constexpr int kHandsetPickerCommandRotate = 1001;
inline constexpr int kHandsetPickerCommandTurnOff = 1002;
inline constexpr int kHandsetPickerCommandMoreDevices = 1003;

// The Handset device picker: every catalogue profile with a check mark on
// the live one, a custom-size dialog for typing arbitrary dimensions, and -
// once a Handset is live - rotate and turn off.
//
// Owned by the location bar as a member, not constructed fresh per click.
// ui::MenuRunner shows the menu asynchronously, so the model and the runner
// have to outlive the call that opens them; a locally-scoped instance would
// be destroyed before the user had a chance to click anything in it.
class HandsetPickerMenu : public ui::SimpleMenuModel::Delegate {
 public:
  HandsetPickerMenu();
  HandsetPickerMenu(const HandsetPickerMenu&) = delete;
  HandsetPickerMenu& operator=(const HandsetPickerMenu&) = delete;
  ~HandsetPickerMenu() override;

  // Opens the picker anchored to `anchor`, acting on `web_contents`. Any menu
  // this instance already has open is torn down first.
  void Show(views::View* anchor, content::WebContents* web_contents);

  // Builds the model without creating a platform menu widget, then calls
  // ExecuteCommand(command_id) directly - the same effect a click on that
  // item would have, without the interactive, asynchronous MenuRunner in the
  // loop. Lets a browser test drive real profile selection, rotation, and
  // turn-off through the actual delegate logic, the same split
  // seoul_workspace_name_dialog.h uses between building a DialogModel and
  // showing it as a platform window.
  void ExecuteCommandForTesting(content::WebContents* web_contents,
                                int command_id);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void BuildModel(content::WebContents* web_contents);
  void ShowCustomSizeDialog();

  // Not owned; the anchor view's widget outlives the asynchronous menu, and
  // the WebContents outlives a single menu interaction in every real case
  // (closing the tab mid-click dismisses the menu with it).
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  std::map<int, std::string> command_to_profile_id_;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  // The "All devices" submenu: the full generated catalogue beyond the
  // featured devices. A SimpleMenuModel does not own its submenus, so this
  // lives beside model_ with the same lifetime.
  std::unique_ptr<ui::SimpleMenuModel> more_model_;
  std::unique_ptr<views::MenuRunner> runner_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_
