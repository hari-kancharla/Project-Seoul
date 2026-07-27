// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/views/seoul_shell_space_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/vector_icons/vector_icons.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/space_visuals.h"
#include "seoul/browser/shell/views/seoul_workspace_menu.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr base::TimeDelta kIndicatorActionTransition =
    base::Milliseconds(100);

std::u16string SpaceIcon(const ShellWorkspaceHeader& workspace) {
  std::u16string icon = base::UTF8ToUTF16(workspace.icon);
  if (!icon.empty()) {
    return icon;
  }
  const std::u16string name = base::UTF8ToUTF16(workspace.name);
  return name.empty() ? u"◈" : name.substr(0, 1);
}

class InteractionTrackingLabelButton final : public views::LabelButton {
 public:
  InteractionTrackingLabelButton(views::Button::PressedCallback callback,
                                 base::RepeatingClosure interaction_changed)
      : views::LabelButton(std::move(callback), std::u16string()),
        interaction_changed_(std::move(interaction_changed)) {}

 private:
  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    interaction_changed_.Run();
  }

  void OnFocus() override {
    views::LabelButton::OnFocus();
    interaction_changed_.Run();
  }

  void OnBlur() override {
    views::LabelButton::OnBlur();
    interaction_changed_.Run();
  }

  base::RepeatingClosure interaction_changed_;
};

class InteractionTrackingImageButton final : public views::ImageButton {
 public:
  InteractionTrackingImageButton(views::Button::PressedCallback callback,
                                 base::RepeatingClosure interaction_changed)
      : views::ImageButton(std::move(callback)),
        interaction_changed_(std::move(interaction_changed)) {}

 private:
  void StateChanged(ButtonState old_state) override {
    views::ImageButton::StateChanged(old_state);
    interaction_changed_.Run();
  }

  void OnFocus() override {
    views::ImageButton::OnFocus();
    interaction_changed_.Run();
  }

  void OnBlur() override {
    views::ImageButton::OnBlur();
    interaction_changed_.Run();
  }

  base::RepeatingClosure interaction_changed_;
};

bool IsInteracting(const views::Button* button) {
  return button &&
         (button->GetState() == views::Button::STATE_HOVERED ||
          button->GetState() == views::Button::STATE_PRESSED ||
          button->HasFocus());
}

void ConfigureIndicatorInkDrop(views::Button* button,
                               bool highlight_on_hover) {
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::UseInkDropForFloodFillRipple(
      views::InkDrop::Get(button), highlight_on_hover,
      /*highlight_on_focus=*/false);
  views::InkDrop::Get(button)->SetBaseColor(kColorToolbarButtonIcon);
  views::InkDrop::Get(button)->SetVisibleOpacity(0.08f);
  views::InkDrop::Get(button)->SetHighlightOpacity(0.08f);
}

}  // namespace

SeoulShellSpaceView::SeoulShellSpaceView(ShellController* controller) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 0, 0, 6), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  std::unique_ptr<views::LabelButton> owned_button =
      std::make_unique<InteractionTrackingLabelButton>(
          base::BindRepeating(&SeoulShellSpaceView::OnPressed,
                              base::Unretained(this)),
          base::BindRepeating(
              &SeoulShellSpaceView::OnInteractionStateChanged,
              base::Unretained(this)));
  button_ = owned_button.get();
  AddChildView(std::move(owned_button));
  button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  button_->SetMinSize(gfx::Size(
      0, space_visuals::GetIndicatorHeight(/*collapsed=*/false)));
  button_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(4, 12, 4, 4)));
  button_->SetEnabledTextColors(kColorToolbarText);
  button_->SetFocusRingCornerRadius(space_visuals::kSwitcherCornerRadius);
  ConfigureIndicatorInkDrop(button_, /*highlight_on_hover=*/false);
  // This view fades as one Space replaces another. LCD subpixel text assumes
  // an opaque target and is therefore invalid while its ancestor layer is
  // translucent; use grayscale antialiasing for the animated label.
  button_->SetTextSubpixelRenderingEnabled(false);
  button_->GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  button_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  button_->GetViewAccessibility().SetIsCollapsed();

  std::unique_ptr<views::ImageButton> owned_menu_button =
      std::make_unique<InteractionTrackingImageButton>(
          base::BindRepeating(&SeoulShellSpaceView::OnPressed,
                              base::Unretained(this)),
          base::BindRepeating(
              &SeoulShellSpaceView::OnInteractionStateChanged,
              base::Unretained(this)));
  menu_button_ = owned_menu_button.get();
  AddChildView(std::move(owned_menu_button));
  menu_button_->SetPreferredSize(
      gfx::Size(space_visuals::kIndicatorActionSize,
                space_visuals::kIndicatorActionSize));
  menu_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets(7)));
  menu_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  menu_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  menu_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kCaretDownIcon,
                                     kColorToolbarButtonIcon, 12));
  menu_button_->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(vector_icons::kCaretDownIcon,
                                     kColorToolbarButtonIconHovered, 12));
  menu_button_->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(vector_icons::kCaretDownIcon,
                                     kColorToolbarButtonIconPressed, 12));
  menu_button_->SetTooltipText(u"Space menu");
  menu_button_->GetViewAccessibility().SetName(u"Open Space menu");
  menu_button_->GetViewAccessibility().SetHasPopup(
      ax::mojom::HasPopup::kMenu);
  menu_button_->SetPaintToLayer();
  menu_button_->layer()->SetFillsBoundsOpaquely(false);
  menu_button_->layer()->SetOpacity(0.0f);
  ConfigureIndicatorInkDrop(menu_button_, /*highlight_on_hover=*/true);

  layout->SetFlexForView(button_, 1);
  BindController(controller);
}

SeoulShellSpaceView::~SeoulShellSpaceView() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SeoulShellSpaceView::BindController(ShellController* controller) {
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

void SeoulShellSpaceView::SetPresentationCollapsed(bool collapsed) {
  if (presentation_collapsed_ == collapsed) {
    return;
  }
  presentation_collapsed_ = collapsed;
  static_cast<views::BoxLayout*>(GetLayoutManager())
      ->set_inside_border_insets(collapsed ? gfx::Insets()
                                           : gfx::Insets::TLBR(0, 0, 0, 6));
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  PreferredSizeChanged();
}

void SeoulShellSpaceView::OnShellSnapshotChanged(
    const ShellChange& change,
    const ShellSnapshot& snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

void SeoulShellSpaceView::RebuildFromSnapshot(const ShellSnapshot& snapshot) {
  const bool animate_switch =
      rendered_workspace_id_.is_valid() &&
      rendered_workspace_id_ != snapshot.workspace.workspace_id;
  rendered_workspace_id_ = snapshot.workspace.workspace_id;
  const std::u16string icon = SpaceIcon(snapshot.workspace);
  const std::u16string name = base::UTF8ToUTF16(snapshot.workspace.name);
  std::u16string label = icon;
  if (!presentation_collapsed_) {
    label += u"  ";
    label += name.empty() ? u"Space" : name;
  }
  button_->SetText(label);
  button_->SetHorizontalAlignment(presentation_collapsed_ ? gfx::ALIGN_CENTER
                                                          : gfx::ALIGN_LEFT);
  button_->SetMinSize(gfx::Size(
      0, space_visuals::GetIndicatorHeight(presentation_collapsed_)));
  button_->SetBorder(views::CreateEmptyBorder(presentation_collapsed_
                                                  ? gfx::Insets::VH(4, 4)
                                                  : gfx::Insets::TLBR(
                                                        4, 12, 4, 4)));
  std::u16string accessible_name = u"Current Space";
  if (!name.empty()) {
    accessible_name += u", " + name;
  }
  if (snapshot.workspace.switching) {
    accessible_name += u", switching";
  }
  accessible_name += u". Open Space menu";
  button_->GetViewAccessibility().SetName(accessible_name);
  button_->SetTooltipText(name.empty() ? u"Current Space" : name);
  std::u16string menu_accessible_name = u"Open Space menu";
  if (!name.empty()) {
    menu_accessible_name += u", " + name;
  }
  menu_button_->GetViewAccessibility().SetName(menu_accessible_name);
  menu_button_->SetVisible(!presentation_collapsed_);
  SetVisible(snapshot.workspace.workspace_id.is_valid());
  UpdateInteractionVisuals(/*animate=*/false);
  if (animate_switch && layer()) {
    layer()->SetOpacity(0.55f);
    views::AnimationBuilder()
        .SetPreemptionStrategy(ui::LayerAnimator::PreemptionStrategy::
                                   IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
        .Once()
        .SetDuration(base::Milliseconds(200))
        .SetOpacity(layer(), 1.0f);
  }
}

void SeoulShellSpaceView::OnPressed() {
  if (!controller_ || !GetWidget() || workspace_menu_open_) {
    return;
  }
  workspace_menu_open_ = true;
  button_->GetViewAccessibility().SetIsExpanded();
  UpdateInteractionVisuals(/*animate=*/true);
  workspace_menu_ = std::make_unique<SeoulWorkspaceMenu>(
      GetWidget()->GetNativeWindow(), button_, controller_);
  if (!workspace_menu_->Show(base::BindRepeating(
          &SeoulShellSpaceView::OnWorkspaceMenuClosed,
          weak_factory_.GetWeakPtr()))) {
    OnWorkspaceMenuClosed();
  }
}

void SeoulShellSpaceView::OnInteractionStateChanged() {
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::OnWorkspaceMenuClosed() {
  if (!workspace_menu_open_) {
    return;
  }
  workspace_menu_open_ = false;
  button_->GetViewAccessibility().SetIsCollapsed();
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::UpdateInteractionVisuals(bool animate) {
  if (!button_ || !menu_button_) {
    return;
  }
  const bool interacting =
      workspace_menu_open_ || IsInteracting(button_) ||
      IsInteracting(menu_button_);
  SetBackground(
      interacting && !presentation_collapsed_
          ? views::CreateRoundedRectBackground(
                kColorToolbarBackgroundSubtleEmphasis,
                space_visuals::kSwitcherCornerRadius)
          : nullptr);

  if (!menu_button_->layer()) {
    return;
  }
  const float opacity = space_visuals::GetIndicatorActionOpacity(
      interacting && !presentation_collapsed_);
  if (animate && gfx::Animation::ShouldRenderRichAnimation()) {
    ui::ScopedLayerAnimationSettings settings(
        menu_button_->layer()->GetAnimator());
    settings.SetTransitionDuration(kIndicatorActionTransition);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    menu_button_->layer()->SetOpacity(opacity);
    return;
  }
  menu_button_->layer()->SetOpacity(opacity);
}

BEGIN_METADATA(SeoulShellSpaceView)
END_METADATA

}  // namespace seoul
