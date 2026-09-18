// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SETTINGS_TOOLBAR_MAC_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SETTINGS_TOOLBAR_MAC_H_

#include <memory>
#include "base/functional/callback.h"
#include "seoul/browser/product/browser/seoul_settings_window.h"

namespace views { class Widget; }
namespace seoul {

class SettingsToolbarMac {
 public:
  virtual ~SettingsToolbarMac() = default;
  virtual void Select(SettingsPane pane) = 0;
  virtual bool ActivateForTesting(SettingsPane pane) = 0;
};
std::unique_ptr<SettingsToolbarMac> CreateSettingsToolbarMac(
    views::Widget* widget, SettingsPane pane,
    base::RepeatingCallback<void(SettingsPane)> select);

}  // namespace seoul
#endif
