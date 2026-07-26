// Project Seoul native browser shell V0.

#include "seoul/browser/shell/views/seoul_shell_footer_view.h"

#include <tuple>

#include "base/strings/utf_string_conversions.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/shell_view_model.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_split_chooser_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kShellCornerRadius = 8;

void StyleUtilityButton(views::LabelButton *button) {
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 9)));
  button->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1, kShellCornerRadius));
  button->SetEnabledTextColors(ui::kColorSysOnSurface);
}

} // namespace

SeoulShellFooterView::SeoulShellFooterView(ShellController *controller) {
  auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(4, 8, 6, 8),
      4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  BindController(controller);
}

SeoulShellFooterView::~SeoulShellFooterView() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SeoulShellFooterView::BindController(ShellController *controller) {
  if (controller_ == controller) {
    return;
  }
  if (controller_) {
    controller_->RemoveObserver(this);
  }
  controller_ = controller;
  if (controller_) {
    controller_->AddObserver(this);
    RebuildFromSnapshot(controller_->snapshot());
  }
}

void SeoulShellFooterView::SetPresentationCollapsed(bool collapsed) {
  if (presentation_collapsed_ == collapsed) {
    return;
  }
  presentation_collapsed_ = collapsed;
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  InvalidateLayout();
  PreferredSizeChanged();
}

void SeoulShellFooterView::OnShellSnapshotChanged(
    const ShellChange &change, const ShellSnapshot &snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

void SeoulShellFooterView::RebuildFromSnapshot(const ShellSnapshot &snapshot) {
  if (!boost_button_) {
    auto *row = AddChildView(std::make_unique<views::View>());
    button_row_layout_ =
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    new_tab_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnNewTabPressed,
                            base::Unretained(this)),
        u"＋  New tab"));
    canvas_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnCanvasPressed,
                            base::Unretained(this)),
        u"◇  Seoul Canvas"));
    boost_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnBoostPressed,
                            base::Unretained(this)),
        u"⚡  Boost this site"));
    task_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnTaskDeckPressed,
                            base::Unretained(this)),
        u"◫  Tasks"));
    split_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnSplitPressed,
                            base::Unretained(this)),
        u"↔  Split view"));
    launcher_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnCommandLauncherPressed,
                            base::Unretained(this)),
        u"⌕  Commands"));
    compact_button_ = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnCompactPressed,
                            base::Unretained(this)),
        u"⇤  Compact mode"));
    reconcile_button_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulShellFooterView::OnReconcilePressed,
                            base::Unretained(this)),
        u"Recover"));
    StyleUtilityButton(new_tab_button_);
    StyleUtilityButton(canvas_button_);
    StyleUtilityButton(boost_button_);
    StyleUtilityButton(launcher_button_);
    StyleUtilityButton(task_button_);
    StyleUtilityButton(split_button_);
    StyleUtilityButton(compact_button_);
    StyleUtilityButton(reconcile_button_);
    button_row_layout_->SetFlexForView(canvas_button_, 1);
    button_row_layout_->SetFlexForView(boost_button_, 1);
    button_row_layout_->SetFlexForView(task_button_, 1);
    button_row_layout_->SetFlexForView(split_button_, 1);
    button_row_layout_->SetFlexForView(launcher_button_, 1);
    button_row_layout_->SetFlexForView(compact_button_, 1);
    new_tab_button_->GetViewAccessibility().SetName(u"Open new tab");
    canvas_button_->GetViewAccessibility().SetName(u"Open Seoul Canvas");
    boost_button_->GetViewAccessibility().SetName(
        u"Create or manage Boosts for this site");
    launcher_button_->GetViewAccessibility().SetName(u"Command launcher");
    task_button_->GetViewAccessibility().SetName(u"Task Deck, no tasks");
    split_button_->GetViewAccessibility().SetName(u"Create split");
    compact_button_->GetViewAccessibility().SetName(u"Toggle compact mode");
    reconcile_button_->GetViewAccessibility().SetName(u"Run reconciliation");
    status_label_ = AddChildView(std::make_unique<views::Label>(
        u"", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    status_label_->GetViewAccessibility().SetLiveRegionContainer(
        views::ViewAccessibility::LiveRegionStatus::kPolite);
    empty_state_view_ = AddChildView(std::make_unique<views::View>());
    auto *empty_layout =
        empty_state_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    empty_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    auto *empty_label = empty_state_view_->AddChildView(
        std::make_unique<views::Label>(u"No tabs in this workspace"));
    empty_label->GetViewAccessibility().SetName(u"Empty workspace");
    auto *empty_action =
        empty_state_view_->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SeoulShellFooterView::OnNewTabPressed,
                                base::Unretained(this)),
            u"Open New Tab"));
    empty_action->GetViewAccessibility().SetName(u"Open new tab");
  }

  auto action_enabled = [&](ShellUtilityAction action) {
    for (const ShellActionEnablement &entry : snapshot.actions) {
      if (entry.action == action) {
        return entry;
      }
    }
    return ShellActionEnablement();
  };

  const ShellActionEnablement boost =
      action_enabled(ShellUtilityAction::kOpenBoost);
  const ShellActionEnablement new_tab =
      action_enabled(ShellUtilityAction::kNewTemporaryTab);
  const ShellActionEnablement canvas =
      action_enabled(ShellUtilityAction::kOpenCanvas);
  const ShellActionEnablement split =
      action_enabled(ShellUtilityAction::kCreateSplit);
  const ShellActionEnablement reconcile =
      action_enabled(ShellUtilityAction::kReconcile);
  const ShellActionEnablement task_deck =
      action_enabled(ShellUtilityAction::kOpenTaskDeck);
  const ShellActionEnablement compact =
      action_enabled(ShellUtilityAction::kToggleCompactMode);
  const bool collapsed = presentation_collapsed_;
  button_row_layout_->SetOrientation(
      views::BoxLayout::Orientation::kHorizontal);

  // The native tab strip already owns New Tab. Keep high-frequency product
  // destinations here; the remaining browser utilities live in Commands.
  new_tab_button_->SetVisible(false);
  new_tab_button_->SetText(u"New tab");
  new_tab_button_->SetEnabled(new_tab.enabled);
  new_tab_button_->SetTooltipText(
      new_tab.enabled ? u"Open a new Seoul tab"
                      : base::UTF8ToUTF16(new_tab.disabled_reason));
  canvas_button_->SetText(u"◇");
  canvas_button_->SetVisible(!collapsed);
  canvas_button_->SetEnabled(canvas.enabled);
  canvas_button_->SetTooltipText(
      canvas.enabled ? u"Open Seoul Canvas"
                     : base::UTF8ToUTF16(canvas.disabled_reason));
  boost_button_->SetText(u"⚡");
  boost_button_->SetVisible(!collapsed);
  boost_button_->SetTooltipText(boost.enabled
                                    ? u"Create or manage Boosts for this site"
                                    : base::UTF8ToUTF16(boost.disabled_reason));
  boost_button_->SetEnabled(boost.enabled);
  launcher_button_->SetText(u"⋯");
  launcher_button_->SetVisible(!collapsed);
  launcher_button_->SetTooltipText(u"Search commands");
  task_button_->SetText(base::UTF8ToUTF16(
      ShellViewModel::TaskButtonLabel(snapshot.tasks, ShellMode::kCollapsed)));
  task_button_->GetViewAccessibility().SetName(
      base::UTF8ToUTF16(ShellViewModel::TaskAccessibleName(snapshot.tasks)));
  task_button_->SetTooltipText(
      task_deck.enabled ? u"Open Task Deck"
                        : base::UTF8ToUTF16(task_deck.disabled_reason));
  task_button_->SetEnabled(task_deck.enabled);
  task_button_->SetVisible(!collapsed);
  split_button_->SetText(u"↔");
  split_button_->SetTooltipText(split.enabled
                                    ? u"Create split"
                                    : base::UTF8ToUTF16(split.disabled_reason));

  split_button_->SetEnabled(split.enabled);
  split_button_->SetVisible(!collapsed);
  compact_button_->SetText(snapshot.compact_mode.enabled ? u"⇥" : u"⇤");
  compact_button_->SetEnabled(compact.enabled);
  compact_button_->SetTooltipText(
      compact.enabled ? u"Toggle compact sidebar"
                      : base::UTF8ToUTF16(compact.disabled_reason));
  compact_button_->SetVisible(!collapsed);
  if (snapshot.status == ShellStatus::kRecoveryRequired) {
    reconcile_button_->SetVisible(true);
    reconcile_button_->SetText(collapsed ? u"!" : u"Acknowledge Recovery");
    reconcile_button_->GetViewAccessibility().SetName(u"Acknowledge recovery");
    reconcile_button_->SetEnabled(true);
  } else {
    reconcile_button_->SetText(u"Recover");
    reconcile_button_->GetViewAccessibility().SetName(u"Run reconciliation");
    reconcile_button_->SetVisible(!collapsed && snapshot.show_status_banner);
    reconcile_button_->SetEnabled(reconcile.enabled);
  }
  reconcile_button_->SetTooltipText(
      snapshot.status == ShellStatus::kRecoveryRequired
          ? u"Acknowledge recovery"
      : reconcile.enabled ? u"Run reconciliation"
                          : base::UTF8ToUTF16(reconcile.disabled_reason));

  if (snapshot.show_status_banner && !collapsed) {
    status_label_->SetText(base::UTF8ToUTF16(snapshot.status_message));
    status_label_->SetVisible(true);
    status_label_->GetViewAccessibility().SetName(
        base::UTF8ToUTF16(snapshot.status_message));
  } else {
    status_label_->SetVisible(false);
  }
  empty_state_view_->SetVisible(snapshot.show_empty_workspace && !collapsed);
  SetVisible(true);
}

void SeoulShellFooterView::OnNewTabPressed() {
  if (controller_) {
    std::ignore =
        controller_->RunUtilityAction(ShellUtilityAction::kNewTemporaryTab);
  }
}

void SeoulShellFooterView::OnCanvasPressed() {
  if (controller_) {
    std::ignore =
        controller_->RunUtilityAction(ShellUtilityAction::kOpenCanvas);
  }
}

void SeoulShellFooterView::OnBoostPressed() {
  if (controller_) {
    std::ignore = controller_->RunUtilityAction(ShellUtilityAction::kOpenBoost);
  }
}

void SeoulShellFooterView::OnCommandLauncherPressed() {
  if (!controller_ || !GetWidget()) {
    return;
  }
  SeoulCommandLauncherView::Show(GetWidget()->GetNativeWindow(),
                                 launcher_button_, controller_);
}

void SeoulShellFooterView::OnTaskDeckPressed() {
  if (controller_) {
    std::ignore =
        controller_->RunUtilityAction(ShellUtilityAction::kOpenTaskDeck);
  }
}

void SeoulShellFooterView::OnSplitPressed() {
  if (controller_ && GetWidget()) {
    SeoulSplitChooserView::Show(GetWidget()->GetNativeWindow(), split_button_,
                                controller_);
  }
}

void SeoulShellFooterView::OnCompactPressed() {
  if (controller_) {
    std::ignore = controller_->ToggleCompactMode();
  }
}

void SeoulShellFooterView::OnReconcilePressed() {
  if (!controller_) {
    return;
  }
  if (controller_->snapshot().status == ShellStatus::kRecoveryRequired) {
    std::ignore = controller_->AcknowledgeRecovery();
    return;
  }
  std::ignore = controller_->RunReconciliation();
}

BEGIN_METADATA(SeoulShellFooterView)
END_METADATA

} // namespace seoul
