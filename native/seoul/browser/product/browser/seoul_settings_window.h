// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SETTINGS_WINDOW_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SETTINGS_WINDOW_H_

#include <optional>

class Profile;
namespace views {
class Widget;
}

namespace seoul {

// Owned by the selected profile's runtime service. Shutdown destroys the
// native window before that profile's dependent services are torn down.
class SettingsWindowOwner {
 public:
  virtual ~SettingsWindowOwner() = default;
};

enum class SettingsPane {
  kGeneral = 0,
  kAppearance = 1,
  kProfiles = 2,
  kShortcuts = 3,
  kPrivacy = 4,
  kAdvanced = 5,
};

inline constexpr char kSettingsLastPanePref[] = "seoul.settings.last_pane";

// Opens or activates the single native Settings window. Private-window entry
// points edit the original regular profile; guest/system profiles are refused.
bool ShowSeoulSettings(Profile* profile,
                       std::optional<SettingsPane> pane = std::nullopt);

bool ActivateSeoulSettingsPaneForTesting(SettingsPane pane);
views::Widget* GetSeoulSettingsWidgetForTesting();
Profile* GetSeoulSettingsProfileForTesting();

}  // namespace seoul
#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SETTINGS_WINDOW_H_
