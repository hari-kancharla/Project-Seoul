// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/views/seoul_shell_footer_view.h"

#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/space_visuals.h"
#include "seoul/browser/shell/views/seoul_command_launcher_view.h"
#include "seoul/browser/shell/views/seoul_split_chooser_view.h"
#include "seoul/browser/shell/workspace_icon_painter.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
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
constexpr base::TimeDelta kCreateNewRotationDuration = base::Milliseconds(200);

void StyleFooterButton(views::LabelButton* button) {
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(kFooterButtonSize, kFooterButtonSize));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 7)));
  button->SetEnabledTextColors(kColorToolbarText);
}

void SetFooterIcon(views::LabelButton* button, const gfx::VectorIcon& icon) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, kColorToolbarButtonIcon, 18));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(icon, kColorToolbarButtonIconHovered, 18));
  button->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(icon, kColorToolbarButtonIconPressed, 18));
  button->SetImageModel(views::Button::STATE_DISABLED,
                        ui::ImageModel::FromVectorIcon(
                            icon, kColorToolbarButtonIconDisabled, 18));
}

class CreateNewButton final : public views::LabelButton {
 public:
  METADATA_HEADER(CreateNewButton, views::LabelButton)

 public:
  explicit CreateNewButton(views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), std::u16string()) {
    image_container_view()->SetPaintToLayer();
    image_container_view()->layer()->SetFillsBoundsOpaquely(false);
  }

  void SetLauncherVisible(bool visible) {
    if (launcher_visible_ == visible) {
      return;
    }
    launcher_visible_ = visible;
    ApplyRotation(/*animate=*/true);
  }

  views::View* icon_view_for_testing() const {
    return const_cast<views::View*>(image_container_view());
  }

 private:
  void ApplyRotation(bool animate) {
    views::View* const icon = image_container_view();
    if (!icon->layer()) {
      return;
    }

    gfx::Transform rotation;
    rotation.Rotate(launcher_visible_ ? 45.0 : 0.0);
    const gfx::Transform target = gfx::TransformAboutPivot(
        gfx::PointF(icon->GetLocalBounds().CenterPoint()), rotation);

    if (animate && gfx::Animation::ShouldRenderRichAnimation()) {
      ui::ScopedLayerAnimationSettings settings(icon->layer()->GetAnimator());
      settings.SetTransitionDuration(kCreateNewRotationDuration);
      settings.SetTweenType(gfx::Tween::EASE_IN_OUT);
      settings.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      icon->layer()->SetTransform(target);
      return;
    }
    icon->layer()->GetAnimator()->StopAnimating();
    icon->layer()->SetTransform(target);
  }

  bool launcher_visible_ = false;
};

BEGIN_METADATA(CreateNewButton)
END_METADATA

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
    builtin_icon_ref_.clear();
    if (IsWorkspaceBuiltinIcon(space.icon)) {
      builtin_icon_ref_ = space.icon;
    }
    const std::u16string icon = builtin_icon_ref_.empty()
                                    ? base::UTF8ToUTF16(space.icon)
                                    : std::u16string();
    uses_empty_icon_dot_ = space.icon.empty();
    if (uses_empty_icon_dot_ || !builtin_icon_ref_.empty()) {
      SetText(std::u16string());
      SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
      SetImageModel(views::Button::STATE_HOVERED, ui::ImageModel());
      SetImageModel(views::Button::STATE_PRESSED, ui::ImageModel());
      SetImageModel(views::Button::STATE_DISABLED, ui::ImageModel());
    } else {
      SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
      SetImageModel(views::Button::STATE_HOVERED, ui::ImageModel());
      SetImageModel(views::Button::STATE_PRESSED, ui::ImageModel());
      SetImageModel(views::Button::STATE_DISABLED, ui::ImageModel());
      SetText(icon);
    }
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

  bool uses_empty_icon_dot_for_testing() const { return uses_empty_icon_dot_; }

 private:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!GetColorProvider()) {
      return;
    }
    if (!builtin_icon_ref_.empty()) {
      ui::ColorId color_id = kColorToolbarButtonIcon;
      if (GetState() == views::Button::STATE_HOVERED) {
        color_id = kColorToolbarButtonIconHovered;
      } else if (GetState() == views::Button::STATE_PRESSED) {
        color_id = kColorToolbarButtonIconPressed;
      } else if (GetState() == views::Button::STATE_DISABLED) {
        color_id = kColorToolbarButtonIconDisabled;
      }
      gfx::Rect icon_bounds = GetLocalBounds();
      icon_bounds.ClampToCenteredSize(gfx::Size(18, 18));
      PaintWorkspaceBuiltinIcon(canvas, builtin_icon_ref_, icon_bounds,
                                GetColorProvider()->GetColor(color_id));
      return;
    }
    if (!uses_empty_icon_dot_) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(
        SkColorSetA(GetColorProvider()->GetColor(kColorToolbarButtonIcon),
                    /*a=*/102));
    canvas->DrawCircle(
        gfx::RectF(GetLocalBounds()).CenterPoint(),
        static_cast<float>(space_visuals::kEmptyIconDiameter) / 2.0f, flags);
  }

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

  bool active_ = false;
  bool uses_empty_icon_dot_ = false;
  std::string builtin_icon_ref_;
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

  // Zen's production default is intentionally only Toggle Sidebar, Workspaces,
  // and Create New, in that order. The flexible middle control produces the
  // same expanded space-between distribution without placeholder controls.
  toggle_sidebar_button_ =
      controls_row_->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SeoulShellFooterView::OnToggleSidebarPressed,
                              base::Unretained(this)),
          std::u16string()));
  StyleFooterButton(toggle_sidebar_button_);
  SetFooterIcon(toggle_sidebar_button_, kSeoulExpandSidebarIcon);

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

  create_new_button_ = controls_row_->AddChildView(
      std::make_unique<CreateNewButton>(base::BindRepeating(
          &SeoulShellFooterView::OnCreateNewPressed, base::Unretained(this))));
  StyleFooterButton(create_new_button_);
  SetFooterIcon(create_new_button_, kSeoulPlusIcon);
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

  toggle_sidebar_button_->SetVisible(true);
  const bool sidebar_collapsed =
      snapshot.appearance_layout.available &&
      snapshot.appearance_layout.mode == ShellAppearanceLayoutMode::kCollapsed;
  const std::u16string toggle_sidebar_label =
      sidebar_collapsed ? u"Expand Sidebar" : u"Collapse Sidebar";
  toggle_sidebar_button_->SetTooltipText(toggle_sidebar_label);
  toggle_sidebar_button_->GetViewAccessibility().SetName(toggle_sidebar_label);
  toggle_sidebar_button_->SetEnabled(snapshot.appearance_layout.available);
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

void SeoulShellFooterView::OnToggleSidebarPressed() {
  if (!controller_) {
    return;
  }
  const ShellAppearanceLayoutState& appearance =
      controller_->snapshot().appearance_layout;
  if (!appearance.available) {
    return;
  }
  const ShellAppearanceLayoutMode target =
      appearance.mode == ShellAppearanceLayoutMode::kCollapsed
          ? ShellAppearanceLayoutMode::kSingle
          : ShellAppearanceLayoutMode::kCollapsed;
  std::ignore = controller_->SetAppearanceLayoutMode(target);
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

void SeoulShellFooterView::SetCommandLauncherVisible(bool visible) {
  if (command_launcher_visible_ == visible) {
    return;
  }
  command_launcher_visible_ = visible;
  if (create_new_button_) {
    static_cast<CreateNewButton*>(create_new_button_.get())
        ->SetLauncherVisible(visible);
  }
}

views::View* SeoulShellFooterView::create_new_icon_for_testing() const {
  return create_new_button_
             ? static_cast<CreateNewButton*>(create_new_button_.get())
                   ->icon_view_for_testing()
             : nullptr;
}

bool SeoulShellFooterView::first_space_uses_empty_icon_dot_for_testing() const {
  return !space_buttons_.empty() &&
         static_cast<const SpaceSwitcherButton*>(space_buttons_.front().get())
             ->uses_empty_icon_dot_for_testing();
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
