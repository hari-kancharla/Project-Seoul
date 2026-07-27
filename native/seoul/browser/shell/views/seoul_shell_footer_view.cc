// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/views/seoul_shell_footer_view.h"

#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/vector_icons/vector_icons.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/space_visuals.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_split_chooser_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kFooterButtonSize = 34;
constexpr base::TimeDelta kSpaceVisualTransition = base::Milliseconds(200);

void StyleFooterButton(views::LabelButton* button) {
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(kFooterButtonSize, kFooterButtonSize));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 7)));
  button->SetEnabledTextColors(kColorToolbarText);
}

class SpaceSwitcherButton final : public views::LabelButton {
 public:
  SpaceSwitcherButton(views::Button::PressedCallback callback,
                      const ShellSpaceItem& space)
      : views::LabelButton(std::move(callback), std::u16string()) {
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetPreferredSize(gfx::Size(space_visuals::kSwitcherButtonSize,
                               space_visuals::kSwitcherButtonSize));
    SetMinSize(gfx::Size(space_visuals::kSwitcherButtonSize,
                         space_visuals::kSwitcherButtonSize));
    SetMaxSize(gfx::Size(space_visuals::kSwitcherButtonSize,
                         space_visuals::kSwitcherButtonSize));
    SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    SetEnabledTextColors(kColorToolbarText);
    SetFocusRingCornerRadius(space_visuals::kSwitcherCornerRadius);
    SetTextSubpixelRenderingEnabled(false);
#if BUILDFLAG(IS_MAC)
    label()->SetFontList(label()->font_list().DeriveWithSizeDelta(
        14 - label()->font_list().GetFontSize()));
#endif

    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InkDrop::UseInkDropForFloodFillRipple(views::InkDrop::Get(this),
                                                 /*highlight_on_hover=*/true,
                                                 /*highlight_on_focus=*/false);
    views::InkDrop::Get(this)->SetBaseColor(kColorToolbarButtonIcon);
    views::InkDrop::Get(this)->SetVisibleOpacity(0.10f);
    views::InkDrop::Get(this)->SetHighlightOpacity(0.10f);

    UpdateSpace(space, /*animate=*/false);
  }

  void UpdateSpace(const ShellSpaceItem& space, bool animate) {
    const std::u16string icon = base::UTF8ToUTF16(space.icon);
    draw_empty_icon_ = icon.empty();
    SetText(icon);
    SetActive(space.is_active, animate);

    const std::u16string name = base::UTF8ToUTF16(space.name);
    SetTooltipText(name.empty() ? u"Space" : name);
    std::u16string accessible_name =
        space.is_active ? u"Current Space" : u"Switch to Space";
    if (!name.empty()) {
      accessible_name += u", " + name;
    }
    if (space.switching) {
      accessible_name += u", switching";
    }
    GetViewAccessibility().SetName(accessible_name);
    SchedulePaint();
  }

 private:
  void SetActive(bool active, bool animate) {
    if (active_ == active && animate) {
      return;
    }
    active_ = active;
    ApplyVisualState(animate);
  }

  void ApplyVisualState(bool animate) {
    if (!layer()) {
      return;
    }
    const bool hovered_or_pressed =
        GetState() == views::Button::STATE_HOVERED ||
        GetState() == views::Button::STATE_PRESSED;
    const space_visuals::SwitcherButtonVisualState visuals =
        space_visuals::GetSwitcherButtonVisualState(active_,
                                                    hovered_or_pressed);
    if (animate && gfx::Animation::ShouldRenderRichAnimation()) {
      ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
      settings.SetTransitionDuration(kSpaceVisualTransition);
      settings.SetTweenType(gfx::Tween::EASE_OUT);
      settings.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      layer()->SetOpacity(visuals.opacity);
      layer()->SetLayerGrayscale(visuals.grayscale);
      return;
    }
    layer()->SetOpacity(visuals.opacity);
    layer()->SetLayerGrayscale(visuals.grayscale);
  }

  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    ApplyVisualState(/*animate=*/true);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::LabelButton::PaintButtonContents(canvas);
    if (!draw_empty_icon_ || !GetColorProvider()) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        SkColorSetA(GetColorProvider()->GetColor(kColorToolbarText), 0x66));
    canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                       space_visuals::kEmptyIconDiameter / 2.0f, flags);
  }

  bool active_ = false;
  bool draw_empty_icon_ = false;
};

}  // namespace

SeoulShellFooterView::SeoulShellFooterView(ShellController* controller) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(2, 6, 6, 6),
      4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  controls_row_ = AddChildView(std::make_unique<views::View>());
  controls_layout_ =
      controls_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  controls_layout_->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Zen's production default is intentionally only Downloads, Workspaces, and
  // Create New, in that order. The flexible middle control produces the same
  // expanded space-between distribution without placeholder controls.
  downloads_button_ =
      controls_row_->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SeoulShellFooterView::OnDownloadsPressed,
                              base::Unretained(this)),
          std::u16string()));
  StyleFooterButton(downloads_button_);
  downloads_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kFileDownloadIcon,
                                     kColorToolbarButtonIcon, 16));
  downloads_button_->SetTooltipText(u"Downloads");
  downloads_button_->GetViewAccessibility().SetName(u"Open Downloads");

  spaces_container_ =
      controls_row_->AddChildView(std::make_unique<views::View>());
  spaces_layout_ =
      spaces_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          space_visuals::kSwitcherGap));
  spaces_layout_->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  spaces_layout_->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  spaces_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  spaces_container_->GetViewAccessibility().SetName(u"Workspaces");
  controls_layout_->SetFlexForView(spaces_container_, 1);

  create_new_button_ =
      controls_row_->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SeoulShellFooterView::OnCreateNewPressed,
                              base::Unretained(this)),
          std::u16string()));
  StyleFooterButton(create_new_button_);
  create_new_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kAddIcon,
                                     kColorToolbarButtonIcon, 16));
  create_new_button_->SetTooltipText(u"Create New");
  create_new_button_->GetViewAccessibility().SetName(u"Create New");

  reconcile_button_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&SeoulShellFooterView::OnReconcilePressed,
                          base::Unretained(this)),
      u"Recover"));
  StyleFooterButton(reconcile_button_);
  reconcile_button_->GetViewAccessibility().SetName(u"Run reconciliation");

  status_label_ = AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
  status_label_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);

  BindController(controller);
}

SeoulShellFooterView::~SeoulShellFooterView() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SeoulShellFooterView::BindController(ShellController* controller) {
  if (controller_ == controller) {
    return;
  }
  if (controller_) {
    controller_->RemoveObserver(this);
  }
  controller_ = controller;
  split_chooser_ = controller_ ? std::make_unique<SeoulSplitChooserView>(
                                     create_new_button_, controller_)
                               : nullptr;
  rendered_spaces_.clear();
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
  controls_layout_->ClearFlexForView(spaces_container_);
  controls_layout_->SetOrientation(
      collapsed ? views::BoxLayout::Orientation::kVertical
                : views::BoxLayout::Orientation::kHorizontal);
  spaces_layout_->SetOrientation(
      collapsed ? views::BoxLayout::Orientation::kVertical
                : views::BoxLayout::Orientation::kHorizontal);
  if (!collapsed) {
    controls_layout_->SetFlexForView(spaces_container_, 1);
  }
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  controls_row_->InvalidateLayout();
  spaces_container_->InvalidateLayout();
  InvalidateLayout();
  PreferredSizeChanged();
}

void SeoulShellFooterView::OnShellSnapshotChanged(
    const ShellChange& change,
    const ShellSnapshot& snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

void SeoulShellFooterView::RebuildFromSnapshot(const ShellSnapshot& snapshot) {
  if (!CanUpdateSpaceButtons(snapshot)) {
    RebuildSpaceButtons(snapshot);
  } else if (rendered_spaces_ != snapshot.spaces) {
    UpdateSpaceButtons(snapshot, /*animate=*/true);
  }

  downloads_button_->SetVisible(true);
  spaces_container_->SetVisible(!snapshot.spaces.empty());
  create_new_button_->SetVisible(true);

  if (snapshot.status == ShellStatus::kRecoveryRequired) {
    reconcile_button_->SetVisible(true);
    reconcile_button_->SetText(
        presentation_collapsed_ ? u"!" : u"Acknowledge Recovery");
    reconcile_button_->GetViewAccessibility().SetName(u"Acknowledge recovery");
  } else {
    reconcile_button_->SetText(u"Recover");
    reconcile_button_->GetViewAccessibility().SetName(u"Run reconciliation");
    reconcile_button_->SetVisible(!presentation_collapsed_ &&
                                  snapshot.show_status_banner);
  }

  if (snapshot.show_status_banner && !presentation_collapsed_) {
    status_label_->SetText(base::UTF8ToUTF16(snapshot.status_message));
    status_label_->SetVisible(true);
    status_label_->GetViewAccessibility().SetName(
        base::UTF8ToUTF16(snapshot.status_message));
  } else {
    status_label_->SetVisible(false);
  }
}

bool SeoulShellFooterView::CanUpdateSpaceButtons(
    const ShellSnapshot& snapshot) const {
  if (rendered_spaces_.size() != snapshot.spaces.size() ||
      space_buttons_.size() != snapshot.spaces.size()) {
    return false;
  }
  for (size_t i = 0; i < snapshot.spaces.size(); ++i) {
    if (rendered_spaces_[i].workspace_id != snapshot.spaces[i].workspace_id) {
      return false;
    }
  }
  return true;
}

void SeoulShellFooterView::RebuildSpaceButtons(const ShellSnapshot& snapshot) {
  spaces_container_->RemoveAllChildViews();
  space_buttons_.clear();
  for (const ShellSpaceItem& space : snapshot.spaces) {
    std::unique_ptr<views::LabelButton> owned_button =
        std::make_unique<SpaceSwitcherButton>(
            base::BindRepeating(&SeoulShellFooterView::OnSpacePressed,
                                base::Unretained(this), space.workspace_id),
            space);
    auto* button = static_cast<SpaceSwitcherButton*>(owned_button.get());
    spaces_container_->AddChildView(std::move(owned_button));
    space_buttons_.push_back(button);
  }
  spaces_container_->SetVisible(!snapshot.spaces.empty());
  rendered_spaces_ = snapshot.spaces;
}

void SeoulShellFooterView::UpdateSpaceButtons(const ShellSnapshot& snapshot,
                                              bool animate) {
  CHECK_EQ(space_buttons_.size(), snapshot.spaces.size());
  for (size_t i = 0; i < snapshot.spaces.size(); ++i) {
    static_cast<SpaceSwitcherButton*>(space_buttons_[i].get())
        ->UpdateSpace(snapshot.spaces[i], animate);
  }
  rendered_spaces_ = snapshot.spaces;
}

void SeoulShellFooterView::OnSpacePressed(WorkspaceId workspace_id) {
  if (controller_) {
    std::ignore = controller_->SwitchWorkspace(workspace_id);
  }
}

void SeoulShellFooterView::OnDownloadsPressed() {
  if (controller_) {
    std::ignore =
        controller_->RunUtilityAction(ShellUtilityAction::kOpenDownloads);
  }
}

bool SeoulShellFooterView::ShowCommandLauncher() {
  if (!controller_ || !GetWidget()) {
    return false;
  }
  SeoulCommandLauncherView::Show(
      GetWidget()->GetNativeWindow(), this, controller_,
      base::BindRepeating(&SeoulShellFooterView::ShowSplitChooser,
                          weak_factory_.GetWeakPtr()));
  return true;
}

void SeoulShellFooterView::OnCreateNewPressed() {
  std::ignore = ShowCommandLauncher();
}

void SeoulShellFooterView::ShowSplitChooser() {
  if (split_chooser_) {
    split_chooser_->Show();
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

}  // namespace seoul
