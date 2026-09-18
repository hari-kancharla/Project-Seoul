// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_

#include "base/memory/weak_ptr.h"

namespace content {
class WebContents;
}
namespace views {
class View;
class Widget;
}  // namespace views

namespace seoul {

inline constexpr int kHandsetPickerCommandCustomSize = 1000;
inline constexpr int kHandsetPickerCommandRotate = 1001;
inline constexpr int kHandsetPickerCommandTurnOff = 1002;

// Owns the lifetime of the searchable native device picker. The public name is
// retained for the existing location-bar integration. Device selection closes
// the picker before changing the tab; no nested platform menu is involved.
class HandsetPickerMenu {
 public:
  HandsetPickerMenu();
  HandsetPickerMenu(const HandsetPickerMenu&) = delete;
  HandsetPickerMenu& operator=(const HandsetPickerMenu&) = delete;
  ~HandsetPickerMenu();

  void Show(views::View* anchor, content::WebContents* web_contents);
  views::Widget* GetWidgetForTesting();

  // Backend coverage complements the real pointer/keyboard picker tests.
  void ExecuteCommandForTesting(content::WebContents* web_contents,
                                int command_id);
  bool IsCommandIdChecked(int command_id) const;

 private:
  base::WeakPtr<content::WebContents> web_contents_;
  base::WeakPtr<views::Widget> widget_;
};

}  // namespace seoul
#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_PICKER_MENU_H_
