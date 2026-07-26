// Project Seoul native browser shell: searchable command launcher.

#include "seoul/browser/shell/views/seoul_command_launcher_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "seoul/browser/shell/command_launcher_catalog.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/views/seoul_split_chooser_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr size_t kMaxLauncherQueryLength = 160;
constexpr int kLauncherWidth = 360;

} // namespace

// Named in the seoul namespace (not anonymous) so it can be added to
// BubbleDialogDelegateView's friend list, which this Chromium revision
// requires of every subclass.
class CommandLauncherBubble final : public views::BubbleDialogDelegateView,
                                    public views::TextfieldController {
public:
  CommandLauncherBubble(views::View *anchor, ShellController *controller)
      : views::BubbleDialogDelegateView(anchor, views::BubbleBorder::TOP_RIGHT),
        controller_(controller),
        entries_(controller->CommandLauncherEntries()) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowCloseButton(true);
    SetTitle(u"Commands");
    SetAccessibleWindowRole(ax::mojom::Role::kDialog);
    SetEnableArrowKeyTraversal(true);
    set_fixed_width(kLauncherWidth);
    auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    query_ = AddChildView(std::make_unique<views::Textfield>());
    query_->SetController(this);
    query_->SetPlaceholderText(u"Search commands");
    query_->SetAccessibleName(u"Search commands");

    result_status_ = AddChildView(std::make_unique<views::Label>(
        u"", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    result_status_->GetViewAccessibility().SetLiveRegionContainer(
        views::ViewAccessibility::LiveRegionStatus::kPolite);

    results_ = AddChildView(std::make_unique<views::View>());
    results_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    RebuildResults(std::string_view());
  }

  CommandLauncherBubble(const CommandLauncherBubble &) = delete;
  CommandLauncherBubble &operator=(const CommandLauncherBubble &) = delete;

  ~CommandLauncherBubble() override {
    if (query_) {
      query_->SetController(nullptr);
    }
  }

  void FocusQuery() { query_->RequestFocus(); }

  void ContentsChanged(views::Textfield *sender,
                       const std::u16string &new_contents) override {
    if (sender != query_) {
      return;
    }
    std::u16string bounded = new_contents.substr(0, kMaxLauncherQueryLength);
    if (bounded.size() != new_contents.size()) {
      sender->SetText(bounded);
      RebuildResults(base::UTF16ToUTF8(bounded));
      return;
    }
    RebuildResults(base::UTF16ToUTF8(bounded));
  }

  bool HandleKeyEvent(views::Textfield *sender,
                      const ui::KeyEvent &event) override {
    if (sender != query_ || event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (event.key_code() == ui::VKEY_RETURN) {
      auto first_enabled = std::ranges::find_if(visible_entries_,
                                                &CommandLauncherEntry::enabled);
      if (first_enabled != visible_entries_.end()) {
        Execute(*first_enabled);
      }
      return true;
    }
    if (event.key_code() == ui::VKEY_DOWN && first_result_) {
      first_result_->RequestFocus();
      return true;
    }
    return false;
  }

private:
  void RebuildResults(std::string_view query) {
    results_->RemoveAllChildViews();
    first_result_ = nullptr;
    visible_entries_ = CommandLauncherCatalog::Filter(entries_, query);
    if (visible_entries_.empty()) {
      result_status_->SetText(u"No matching commands");
      auto *empty = results_->AddChildView(
          std::make_unique<views::Label>(u"No matching commands"));
      empty->GetViewAccessibility().SetName(u"No matching commands");
      results_->InvalidateLayout();
      return;
    }
    result_status_->SetText(
        base::NumberToString16(visible_entries_.size()) +
        (visible_entries_.size() == 1u ? u" command" : u" commands"));
    for (const CommandLauncherEntry &entry : visible_entries_) {
      auto *button =
          results_->AddChildView(std::make_unique<views::LabelButton>(
              base::BindRepeating(&CommandLauncherBubble::Execute,
                                  base::Unretained(this), entry),
              base::UTF8ToUTF16(entry.label)));
      button->SetEnabled(entry.enabled);
      button->GetViewAccessibility().SetName(base::UTF8ToUTF16(entry.label));
      if (!entry.enabled && !entry.disabled_reason.empty()) {
        button->SetTooltipText(base::UTF8ToUTF16(entry.disabled_reason));
        button->GetViewAccessibility().SetDescription(
            base::UTF8ToUTF16(entry.disabled_reason));
      }
      if (!first_result_ && entry.enabled) {
        first_result_ = button;
      }
    }
    results_->InvalidateLayout();
    if (GetWidget()) {
      SizeToContents();
    }
  }

  void Execute(CommandLauncherEntry entry) {
    if (!controller_ || !entry.enabled) {
      return;
    }
    if (entry.action == ShellUtilityAction::kCreateSplit) {
      views::View *anchor = GetAnchorView();
      if (GetWidget()) {
        GetWidget()->Close();
      }
      if (anchor && anchor->GetWidget()) {
        SeoulSplitChooserView::Show(anchor->GetWidget()->GetNativeWindow(),
                                    anchor, controller_);
      }
      return;
    }
    ShellStatusResult result = ShellErr(ShellError::kCommandRejected);
    switch (entry.kind) {
    case CommandLauncherEntryKind::kUtility:
      result = controller_->RunUtilityAction(entry.action);
      break;
    case CommandLauncherEntryKind::kWorkspace: {
      const ShellResult<WorkspaceId> switched =
          controller_->SwitchWorkspace(entry.workspace_id);
      if (switched.has_value()) {
        result = ShellOk();
      } else {
        result = ShellErr(switched.error());
      }
      break;
    }
    case CommandLauncherEntryKind::kEssential:
      result = controller_->OpenEssential(entry.essential_id);
      break;
    case CommandLauncherEntryKind::kTab:
      result = controller_->ActivateLiveTab(entry.live_window, entry.live_tab);
      break;
    }
    if (result.has_value() && GetWidget()) {
      GetWidget()->Close();
    } else {
      result_status_->SetText(u"Command is no longer available. Search again.");
    }
  }

  raw_ptr<ShellController> controller_;
  std::vector<CommandLauncherEntry> entries_;
  std::vector<CommandLauncherEntry> visible_entries_;
  raw_ptr<views::Textfield> query_ = nullptr;
  raw_ptr<views::Label> result_status_ = nullptr;
  raw_ptr<views::View> results_ = nullptr;
  raw_ptr<views::LabelButton> first_result_ = nullptr;
};

void SeoulCommandLauncherView::Show(gfx::NativeWindow parent,
                                    views::View *anchor,
                                    ShellController *controller) {
  (void)parent;
  if (!anchor || !controller || !anchor->GetWidget()) {
    return;
  }
  auto *bubble = new CommandLauncherBubble(anchor, controller);
  views::Widget *widget = views::BubbleDialogDelegateView::CreateBubble(bubble);
  if (gfx::Animation::PrefersReducedMotion()) {
    widget->SetVisibilityChangedAnimationsEnabled(false);
  }
  widget->Show();
  bubble->FocusQuery();
}

} // namespace seoul
