// Project Seoul native browser shell V0.

#include "seoul/browser/shell/views/seoul_shell_header_view.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/views/chrome_layout_provider.h" // nogncheck
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_workspace_menu.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kEssentialOverflowCommandBase = 1;
constexpr int kShellCornerRadius = 8;

void StyleNavigationButton(views::LabelButton *button,
                           bool emphasized = false) {
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 8)));
  button->SetBackground(views::CreateRoundedRectBackground(
      emphasized ? ui::kColorSysSurface3 : ui::kColorSysSurface1,
      kShellCornerRadius));
  button->SetEnabledTextColors(ui::kColorSysOnSurface);
}

std::u16string EssentialLabel(const ShellEssentialItem &essential) {
  return base::UTF8ToUTF16(essential.name.empty() ? essential.root_url
                                                  : essential.name);
}

class EssentialsOverflowMenuModel final : public ui::SimpleMenuModel,
                                          public ui::SimpleMenuModel::Delegate {
public:
  EssentialsOverflowMenuModel(ShellController *controller,
                              std::vector<ShellEssentialItem> essentials)
      : ui::SimpleMenuModel(this), controller_(controller),
        essentials_(std::move(essentials)) {
    for (size_t index = 0; index < essentials_.size(); ++index) {
      AddItem(kEssentialOverflowCommandBase + static_cast<int>(index),
              EssentialLabel(essentials_[index]));
    }
  }

  bool IsCommandIdEnabled(int command_id) const override {
    const int index = command_id - kEssentialOverflowCommandBase;
    return index >= 0 && static_cast<size_t>(index) < essentials_.size() &&
           essentials_[index].state != ShellItemState::kUnavailable;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    (void)event_flags;
    const int index = command_id - kEssentialOverflowCommandBase;
    if (!controller_ || index < 0 ||
        static_cast<size_t>(index) >= essentials_.size() ||
        essentials_[index].state == ShellItemState::kUnavailable) {
      return;
    }
    std::ignore = controller_->OpenEssential(essentials_[index].id);
  }

private:
  raw_ptr<ShellController> controller_;
  std::vector<ShellEssentialItem> essentials_;
};

} // namespace

SeoulShellHeaderView::SeoulShellHeaderView(ShellController *controller) {
  auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(4, 8, 8, 8),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  AddAccelerator(ui::Accelerator(ui::VKEY_K, ui::EF_PLATFORM_ACCELERATOR |
                                                 ui::EF_SHIFT_DOWN));
  AddAccelerator(ui::Accelerator(ui::VKEY_C, ui::EF_PLATFORM_ACCELERATOR |
                                                 ui::EF_SHIFT_DOWN));
  BindController(controller);
}

SeoulShellHeaderView::~SeoulShellHeaderView() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SeoulShellHeaderView::BindController(ShellController *controller) {
  if (controller_ == controller) {
    return;
  }
  if (controller_) {
    controller_->RemoveObserver(this);
  }
  controller_ = controller;
  essentials_initialized_ = false;
  rendered_essentials_.clear();
  overflow_essentials_.clear();
  if (controller_) {
    controller_->AddObserver(this);
    RebuildFromSnapshot(controller_->snapshot());
  }
}

void SeoulShellHeaderView::SetPresentationCollapsed(bool collapsed) {
  if (presentation_collapsed_ == collapsed) {
    return;
  }
  presentation_collapsed_ = collapsed;
  essentials_initialized_ = false;
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  InvalidateLayout();
  PreferredSizeChanged();
}

void SeoulShellHeaderView::OnShellSnapshotChanged(
    const ShellChange &change, const ShellSnapshot &snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

bool SeoulShellHeaderView::AcceleratorPressed(
    const ui::Accelerator &accelerator) {
  const ui::Accelerator launcher(ui::VKEY_K, ui::EF_PLATFORM_ACCELERATOR |
                                                 ui::EF_SHIFT_DOWN);
  const ui::Accelerator compact(ui::VKEY_C, ui::EF_PLATFORM_ACCELERATOR |
                                                ui::EF_SHIFT_DOWN);
  if (!controller_ || !GetWidget()) {
    return false;
  }
  if (accelerator == launcher) {
    OnCommandLauncherPressed();
    return true;
  }
  if (accelerator == compact) {
    std::ignore = controller_->ToggleCompactMode();
    return true;
  }
  return false;
}

void SeoulShellHeaderView::RebuildFromSnapshot(const ShellSnapshot &snapshot) {
  if (!workspace_button_) {
    workspace_row_ = AddChildView(std::make_unique<views::View>());
    auto *workspace_layout =
        workspace_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    workspace_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    workspace_button_ =
        workspace_row_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SeoulShellHeaderView::OnWorkspaceButtonPressed,
                                base::Unretained(this)),
            u"Workspace"));
    workspace_layout->SetFlexForView(workspace_button_, 1);
    StyleNavigationButton(workspace_button_, true);
    workspace_button_->GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    workspace_button_->GetViewAccessibility().SetName(u"Workspace switcher");

    launcher_button_ =
        workspace_row_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SeoulShellHeaderView::OnCommandLauncherPressed,
                                base::Unretained(this)),
            u"⌘"));
    StyleNavigationButton(launcher_button_);
    launcher_button_->SetTooltipText(u"Open command palette");
    launcher_button_->GetViewAccessibility().SetName(u"Open command palette");

    essentials_label_ = AddChildView(std::make_unique<views::Label>(
        u"Essentials", views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    essentials_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    essentials_container_ = AddChildView(std::make_unique<views::View>());
    essentials_layout_ = essentials_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  }

  const bool collapsed = presentation_collapsed_;
  const std::u16string workspace_name =
      base::UTF8ToUTF16(snapshot.workspace.name);
  std::u16string workspace_label = workspace_name;
  if (collapsed) {
    workspace_label = base::UTF8ToUTF16(snapshot.workspace.icon);
    if (workspace_label.empty() && !workspace_name.empty()) {
      workspace_label = workspace_name.substr(0, 1);
    }
  }
  if (snapshot.workspace.switching) {
    workspace_label = u"… " + workspace_label;
  }
  if (!collapsed) {
    workspace_label = base::UTF8ToUTF16(snapshot.workspace.icon.empty()
                                            ? "◈  "
                                            : snapshot.workspace.icon + "  ") +
                      workspace_label + u"  ▾";
  }
  workspace_button_->SetText(workspace_label);
  workspace_button_->SetTooltipText(
      workspace_name.empty() ? u"Workspace switcher" : workspace_name);
  std::u16string workspace_accessible_name = u"Workspace switcher";
  if (!workspace_name.empty()) {
    workspace_accessible_name += u", " + workspace_name;
  }
  if (snapshot.workspace.switching) {
    workspace_accessible_name += u", switching";
  }
  workspace_button_->GetViewAccessibility().SetName(workspace_accessible_name);
  launcher_button_->SetVisible(!collapsed);

  if (!essentials_initialized_ || essentials_collapsed_ != collapsed ||
      rendered_essentials_ != snapshot.essentials) {
    essentials_container_->RemoveAllChildViews();
    essentials_overflow_button_ = nullptr;
    essentials_layout_->SetOrientation(
        collapsed ? views::BoxLayout::Orientation::kVertical
                  : views::BoxLayout::Orientation::kHorizontal);
    constexpr size_t kMaxVisibleEssentials = 5;
    const size_t visible_count =
        std::min(snapshot.essentials.size(), kMaxVisibleEssentials);
    for (size_t index = 0; index < visible_count; ++index) {
      const ShellEssentialItem &essential = snapshot.essentials[index];
      const std::u16string label = EssentialLabel(essential);
      const std::u16string icon =
          essential.icon.empty() ? (label.empty() ? u"★" : label.substr(0, 1))
                                 : base::UTF8ToUTF16(essential.icon);
      auto *button = essentials_container_->AddChildView(
          std::make_unique<views::LabelButton>(
              base::BindRepeating(
                  [](ShellController *controller, EssentialId id) {
                    if (controller) {
                      std::ignore = controller->OpenEssential(id);
                    }
                  },
                  controller_, essential.id),
              icon));
      StyleNavigationButton(button, essential.is_active);
      button->GetViewAccessibility().SetName(label);
      if (essential.is_active) {
        button->GetViewAccessibility().SetDescription(u"Current tab");
      }
      button->SetTooltipText(label);
      button->SetEnabled(essential.state != ShellItemState::kUnavailable);
    }
    overflow_essentials_.assign(snapshot.essentials.begin() + visible_count,
                                snapshot.essentials.end());
    if (!overflow_essentials_.empty()) {
      const std::u16string count =
          base::NumberToString16(overflow_essentials_.size());
      essentials_overflow_button_ = essentials_container_->AddChildView(
          std::make_unique<views::LabelButton>(
              base::BindRepeating(
                  &SeoulShellHeaderView::OnEssentialsOverflowPressed,
                  base::Unretained(this)),
              u"＋"));
      StyleNavigationButton(essentials_overflow_button_);
      essentials_overflow_button_->GetViewAccessibility().SetName(
          count + u" more Essentials");
      essentials_overflow_button_->SetTooltipText(u"Show more Essentials");
    }
    rendered_essentials_ = snapshot.essentials;
    essentials_collapsed_ = collapsed;
    essentials_initialized_ = true;
  }
  essentials_label_->SetVisible(!collapsed && !snapshot.essentials.empty());
  essentials_container_->SetVisible(!snapshot.essentials.empty());
}

void SeoulShellHeaderView::OnWorkspaceButtonPressed() {
  if (!controller_ || !GetWidget()) {
    return;
  }
  SeoulWorkspaceMenu::Show(GetWidget()->GetNativeWindow(), workspace_button_,
                           controller_);
}

void SeoulShellHeaderView::OnCommandLauncherPressed() {
  if (!controller_ || !workspace_button_ || !GetWidget()) {
    return;
  }
  views::View *anchor = launcher_button_ && launcher_button_->GetVisible()
                            ? launcher_button_
                            : workspace_button_;
  SeoulCommandLauncherView::Show(GetWidget()->GetNativeWindow(), anchor,
                                 controller_);
}

void SeoulShellHeaderView::OnEssentialsOverflowPressed() {
  if (!controller_ || !essentials_overflow_button_ ||
      overflow_essentials_.empty() || !GetWidget()) {
    return;
  }
  essentials_overflow_menu_model_ =
      std::make_unique<EssentialsOverflowMenuModel>(controller_,
                                                    overflow_essentials_);
  essentials_overflow_menu_runner_ = std::make_unique<views::MenuRunner>(
      essentials_overflow_menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  essentials_overflow_menu_runner_->RunMenuAt(
      GetWidget(), nullptr,
      essentials_overflow_button_->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kKeyboard);
}

BEGIN_METADATA(SeoulShellHeaderView)
END_METADATA

} // namespace seoul
