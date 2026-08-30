// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/handset_picker_menu.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/handset/handset_types.h"
#include "seoul/browser/handset/viewport_math.h"
#include "seoul/browser/product/browser/handset_mode.h"
#include "seoul/browser/product/browser/seoul_handset_size_dialog.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace seoul {

HandsetPickerMenu::HandsetPickerMenu() = default;
HandsetPickerMenu::~HandsetPickerMenu() = default;

void HandsetPickerMenu::BuildModel(content::WebContents* web_contents) {
  // Tearing down a menu that is still open before building the next one
  // matters here specifically: the button that opens this picker is the same
  // button a second click lands on, and MenuRunner does not like being asked
  // to run a second menu while the first is still alive.
  runner_.reset();
  model_.reset();
  more_model_.reset();
  command_to_profile_id_.clear();

  web_contents_ = web_contents;
  const HandsetProfile* const active = HandsetProfileFor(web_contents_);

  model_ = std::make_unique<ui::SimpleMenuModel>(this);
  more_model_ = std::make_unique<ui::SimpleMenuModel>(this);

  // Top level carries the current device of each family; the full generated
  // catalogue - every phone Chromium itself knows how to emulate - lives one
  // level down so fifty devices never bury the six that matter. One command
  // id per profile either way, so selection, check state, and the tests all
  // go through the same map regardless of which level an item sits on.
  const std::vector<std::string>& featured = FeaturedHandsetProfileIds();
  const auto is_featured = [&featured](const std::string& id) {
    return std::find(featured.begin(), featured.end(), id) != featured.end();
  };
  int command = 0;
  for (const HandsetProfile& profile : HandsetProfiles()) {
    ui::SimpleMenuModel* const level =
        is_featured(profile.id) ? model_.get() : more_model_.get();
    level->AddCheckItem(command, base::UTF8ToUTF16(profile.label));
    command_to_profile_id_[command] = profile.id;
    ++command;
  }
  model_->AddSubMenu(kHandsetPickerCommandMoreDevices, u"All devices",
                     more_model_.get());
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddItem(kHandsetPickerCommandCustomSize, u"Custom size…");
  if (active) {
    model_->AddItem(kHandsetPickerCommandRotate, u"Rotate");
    model_->AddSeparator(ui::NORMAL_SEPARATOR);
    model_->AddItem(kHandsetPickerCommandTurnOff, u"Turn off Handset");
  }
}

void HandsetPickerMenu::Show(views::View* anchor,
                             content::WebContents* web_contents) {
  if (!anchor || !web_contents) {
    return;
  }
  BuildModel(web_contents);

  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::HAS_MNEMONICS);
  // The picker opens from a pointer press on the address-bar button; kMouse
  // is that truth, where kNone makes the menu system guess at positioning
  // and dismissal behaviour.
  runner_->RunMenuAt(anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kMouse);
}

void HandsetPickerMenu::ExecuteCommandForTesting(
    content::WebContents* web_contents,
    int command_id) {
  BuildModel(web_contents);
  ExecuteCommand(command_id, /*event_flags=*/0);
}

bool HandsetPickerMenu::IsCommandIdChecked(int command_id) const {
  const HandsetProfile* const active =
      web_contents_ ? HandsetProfileFor(web_contents_) : nullptr;
  if (!active) {
    return false;
  }
  const auto it = command_to_profile_id_.find(command_id);
  return it != command_to_profile_id_.end() && it->second == active->id;
}

bool HandsetPickerMenu::IsCommandIdEnabled(int command_id) const {
  return true;
}

void HandsetPickerMenu::ExecuteCommand(int command_id, int event_flags) {
  if (!web_contents_) {
    return;
  }
  if (command_id == kHandsetPickerCommandCustomSize) {
    ShowCustomSizeDialog();
    return;
  }
  if (command_id == kHandsetPickerCommandTurnOff) {
    DisableHandsetMode(web_contents_);
    return;
  }
  if (command_id == kHandsetPickerCommandRotate) {
    const HandsetProfile* const active = HandsetProfileFor(web_contents_);
    if (!active) {
      return;
    }
    const HandsetMetrics metrics = HandsetMetricsFor(web_contents_, 0, 0);
    const HandsetOrientation flipped =
        metrics.orientation == HandsetOrientation::kPortrait
            ? HandsetOrientation::kLandscape
            : HandsetOrientation::kPortrait;
    // Rotate re-snaps to the profile's own dimensions in that orientation.
    // A custom free size does not currently survive a rotation - there is no
    // public accessor for the live snap mode to preserve it - so rotating a
    // freely-resized Handset returns it to the nearest catalogue size rather
    // than transposing the typed dimensions. Narrower than ideal, and named
    // here rather than left to be discovered.
    EnableHandsetMode(web_contents_, active->id, flipped,
                      HandsetSnapMode::kSnapToProfile);
    return;
  }
  const auto it = command_to_profile_id_.find(command_id);
  if (it == command_to_profile_id_.end()) {
    return;
  }
  const bool was_active = IsHandsetModeEnabled(web_contents_);
  const HandsetOrientation orientation =
      was_active ? HandsetMetricsFor(web_contents_, 0, 0).orientation
                 : HandsetOrientation::kPortrait;

  // Turning Handset on for the first time opens a dedicated tab for the
  // phone presentation, so the page you were reading stays exactly as it
  // was rather than being converted in place. Once a tab is already showing
  // a Handset, switching device on it (this same menu, reopened) stays on
  // that tab - only the initial turn-on duplicates.
  content::WebContents* target = web_contents_;
  if (!was_active) {
    if (Browser* const browser = chrome::FindBrowserWithTab(web_contents_)) {
      const int index =
          browser->tab_strip_model()->GetIndexOfWebContents(web_contents_);
      if (index != TabStripModel::kNoTab) {
        if (content::WebContents* const duplicate =
                chrome::DuplicateTabAt(browser, index)) {
          target = duplicate;
        }
      }
    }
  }

  EnableHandsetMode(target, it->second, orientation,
                    HandsetSnapMode::kSnapToProfile);
}

void HandsetPickerMenu::ShowCustomSizeDialog() {
  if (!web_contents_) {
    return;
  }
  const bool active = IsHandsetModeEnabled(web_contents_);
  const HandsetProfile* const active_profile =
      active ? HandsetProfileFor(web_contents_) : nullptr;
  const std::string base_profile_id =
      active_profile ? active_profile->id : DefaultHandsetProfile().id;
  const HandsetMetrics metrics =
      active ? HandsetMetricsFor(web_contents_, 0, 0) : HandsetMetrics();
  const HandsetOrientation orientation =
      active ? metrics.orientation : HandsetOrientation::kPortrait;

  ShowHandsetSizeDialog(
      web_contents_->GetTopLevelNativeWindow(),
      active ? metrics.view_width_dip : 0,
      active ? metrics.view_height_dip : 0,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> weak_contents,
             std::string base_profile_id, HandsetOrientation orientation,
             int width_dip, int height_dip) {
            if (!weak_contents) {
              return;
            }
            EnableHandsetMode(weak_contents.get(), base_profile_id,
                              orientation, HandsetSnapMode::kFree, width_dip,
                              height_dip);
          },
          web_contents_->GetWeakPtr(), base_profile_id, orientation));
}

}  // namespace seoul
