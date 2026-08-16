// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/views/seoul_shell_space_view.h"
#include "base/strings/string_util.h"

#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/space_visuals.h"
#include "seoul/browser/shell/views/seoul_workspace_icon_picker.h"
#include "seoul/browser/shell/views/seoul_workspace_menu.h"
#include "seoul/browser/shell/workspace_icon_painter.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr base::TimeDelta kIndicatorActionTransition = base::Milliseconds(100);
constexpr base::TimeDelta kIndicatorGlyphTransition = base::Milliseconds(150);
constexpr int kIndicatorGlyphSize = 16;
constexpr char kZenIndicatorChevronPath[] =
    "M5.97 2.22a.75.75 0 0 1 1.06 0l6.25 6.25a.75.75 0 0 1 0 "
    "1.06l-6.25 6.25a.75.75 0 1 1-1.06-1.06L11.69 9 5.97 3.28a.75.75 "
    "0 0 1 0-1.06";

const SkPath& IndicatorChevronPath() {
  static const base::NoDestructor<const SkPath> path([] {
    std::optional<SkPath> parsed =
        SkParsePath::FromSVGString(kZenIndicatorChevronPath);
    CHECK(parsed.has_value());
    return std::move(parsed.value());
  }());
  return *path;
}

std::u16string ExplicitSpaceIcon(const ShellWorkspaceHeader& workspace) {
  if (!WorkspaceBuiltinIconName(workspace.icon).empty()) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(workspace.icon);
}

std::u16string CollapsedSpaceLabel(const ShellWorkspaceHeader& workspace) {
  const std::u16string icon = ExplicitSpaceIcon(workspace);
  if (!icon.empty()) {
    return icon;
  }
  const std::u16string name = base::UTF8ToUTF16(workspace.name);
  return name.empty() ? u"◈" : name.substr(0, 1);
}

class WorkspaceIndicatorIconView final : public views::Label {
 public:
  WorkspaceIndicatorIconView() {
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetPreferredSize(gfx::Size(kIndicatorGlyphSize, kIndicatorGlyphSize));
    SetSubpixelRenderingEnabled(false);
    SetFontList(font_list().DeriveWithSizeDelta(2).DeriveWithWeight(
        gfx::Font::Weight::NORMAL));
  }

  void SetWorkspaceIcon(std::string icon_ref, std::u16string icon_text) {
    builtin_icon_ref_ = std::move(icon_ref);
    SetText(std::move(icon_text));
    SchedulePaint();
  }

 private:
  void OnPaint(gfx::Canvas* canvas) override {
    if (builtin_icon_ref_.empty()) {
      // A Space with no chosen icon falls back to the first letter of its name,
      // and a bare letter floating in a narrow rail reads as stray text rather
      // than as the Space it stands for. Give it the same rounded tile the tabs
      // beneath it use, so the column is a column of tiles.
      const std::u16string_view text = GetText();
      const bool is_letter_avatar =
          text.size() == 1 && base::IsAsciiAlpha(text[0]);
      if (is_letter_avatar && GetColorProvider()) {
        cc::PaintFlags flags;
        flags.setAntiAlias(true);
        flags.setStyle(cc::PaintFlags::kFill_Style);
        flags.setColor(SkColorSetA(
            GetColorProvider()->GetColor(kColorToolbarButtonIcon), 0x1F));
        canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                              space_visuals::kSwitcherCornerRadius, flags);
      }
      views::Label::OnPaint(canvas);
      return;
    }
    if (!GetColorProvider()) {
      return;
    }
    PaintWorkspaceBuiltinIcon(canvas, builtin_icon_ref_, GetLocalBounds(),
                              GetColorProvider()->GetColor(kColorToolbarText));
  }

  std::string builtin_icon_ref_;
};

class WorkspaceIndicatorChevronView final : public views::View {
 public:
  WorkspaceIndicatorChevronView() {
    SetPreferredSize(gfx::Size(kIndicatorGlyphSize, kIndicatorGlyphSize));
  }

 private:
  void OnPaint(gfx::Canvas* canvas) override {
    if (!GetColorProvider()) {
      return;
    }

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetColorProvider()->GetColor(kColorToolbarText));
    flags.setStyle(cc::PaintFlags::kFill_Style);

    const float scale = static_cast<float>(kIndicatorGlyphSize) / 18.0f;
    canvas->Save();
    canvas->Scale(scale, scale);
    canvas->DrawPath(IndicatorChevronPath(), flags);
    canvas->Restore();
  }
};

class InteractionTrackingGlyphStack final : public views::View {
 public:
  InteractionTrackingGlyphStack(base::RepeatingClosure pressed,
                                base::RepeatingClosure double_clicked)
      : pressed_(std::move(pressed)),
        double_clicked_(std::move(double_clicked)) {
    SetPreferredSize(gfx::Size(kIndicatorGlyphSize, kIndicatorGlyphSize));
    SetLayoutManager(std::make_unique<views::FillLayout>());

    auto owned_icon = std::make_unique<WorkspaceIndicatorIconView>();
    icon_ = owned_icon.get();
    AddChildView(std::unique_ptr<views::View>(std::move(owned_icon)));
    auto owned_chevron = std::make_unique<WorkspaceIndicatorChevronView>();
    chevron_ = owned_chevron.get();
    AddChildView(std::unique_ptr<views::View>(std::move(owned_chevron)));
    for (views::View* view : {static_cast<views::View*>(icon_.get()),
                              static_cast<views::View*>(chevron_.get())}) {
      view->SetPaintToLayer();
      view->layer()->SetFillsBoundsOpaquely(false);
    }
    chevron_->layer()->SetOpacity(0.0f);
  }

  void SetWorkspaceIcon(std::string icon_ref, std::u16string icon_text) {
    has_icon_ = !icon_ref.empty() || !icon_text.empty();
    icon_->SetWorkspaceIcon(std::move(icon_ref), std::move(icon_text));
  }

  void UpdateState(bool has_pinned_tabs,
                   bool pinned_collapsed,
                   bool hovered,
                   bool presentation_collapsed,
                   bool animate) {
    const bool show_chevron = !presentation_collapsed && has_pinned_tabs &&
                              (hovered || pinned_collapsed);
    SetVisible(presentation_collapsed || has_icon_ || show_chevron);

    gfx::Transform chevron_transform;
    if (!pinned_collapsed) {
      chevron_transform.Translate(kIndicatorGlyphSize / 2.0,
                                  kIndicatorGlyphSize / 2.0);
      chevron_transform.Rotate(90.0);
      chevron_transform.Translate(-kIndicatorGlyphSize / 2.0,
                                  -kIndicatorGlyphSize / 2.0);
    }

    if (animate && gfx::Animation::ShouldRenderRichAnimation()) {
      ui::ScopedLayerAnimationSettings icon_settings(
          icon_->layer()->GetAnimator());
      icon_settings.SetTransitionDuration(kIndicatorGlyphTransition);
      icon_settings.SetTweenType(gfx::Tween::EASE_OUT);
      icon_settings.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      icon_->layer()->SetOpacity(show_chevron ? 0.0f : 1.0f);

      ui::ScopedLayerAnimationSettings chevron_settings(
          chevron_->layer()->GetAnimator());
      chevron_settings.SetTransitionDuration(kIndicatorGlyphTransition);
      chevron_settings.SetTweenType(gfx::Tween::EASE_OUT);
      chevron_settings.SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
      chevron_->layer()->SetOpacity(show_chevron ? 1.0f : 0.0f);
      chevron_->layer()->SetTransform(chevron_transform);
      return;
    }

    icon_->layer()->SetOpacity(show_chevron ? 0.0f : 1.0f);
    chevron_->layer()->SetOpacity(show_chevron ? 1.0f : 0.0f);
    chevron_->layer()->SetTransform(chevron_transform);
  }

 private:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return views::View::OnMousePressed(event);
    }
    if (event.GetClickCount() == 2) {
      pressed_.Run();
      double_clicked_.Run();
    } else if (event.GetClickCount() == 1) {
      pressed_.Run();
    }
    return true;
  }

  base::RepeatingClosure pressed_;
  base::RepeatingClosure double_clicked_;
  raw_ptr<WorkspaceIndicatorIconView> icon_ = nullptr;
  raw_ptr<WorkspaceIndicatorChevronView> chevron_ = nullptr;
  bool has_icon_ = false;
};

class InteractionTrackingNameLabel final : public views::Label {
 public:
  InteractionTrackingNameLabel(base::RepeatingClosure pressed,
                               base::RepeatingClosure double_clicked)
      : pressed_(std::move(pressed)),
        double_clicked_(std::move(double_clicked)) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetElideBehavior(gfx::ELIDE_TAIL);
    SetFontList(font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
    SetSubpixelRenderingEnabled(false);
  }

 private:
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    if (GetColorProvider()) {
      SetEnabledColor(
          SkColorSetA(GetColorProvider()->GetColor(kColorToolbarText),
                      static_cast<U8CPU>(space_visuals::kInactiveOpacity *
                                         SK_AlphaOPAQUE)));
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return views::Label::OnMousePressed(event);
    }
    if (event.GetClickCount() == 2) {
      pressed_.Run();
      double_clicked_.Run();
    } else if (event.GetClickCount() == 1) {
      pressed_.Run();
    }
    return true;
  }

  base::RepeatingClosure pressed_;
  base::RepeatingClosure double_clicked_;
};

class WorkspaceNameTextfield final : public views::Textfield {
 public:
  explicit WorkspaceNameTextfield(base::RepeatingClosure blurred)
      : blurred_(std::move(blurred)) {}

 private:
  void OnBlur() override {
    views::Textfield::OnBlur();
    blurred_.Run();
  }

  void OnThemeChanged() override {
    views::Textfield::OnThemeChanged();
    if (GetColorProvider()) {
      SetTextColor(GetColorProvider()->GetColor(kColorToolbarText));
      SetBackgroundColor(SK_ColorTRANSPARENT);
    }
  }

  base::RepeatingClosure blurred_;
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
  return button && (button->GetState() == views::Button::STATE_HOVERED ||
                    button->GetState() == views::Button::STATE_PRESSED ||
                    button->HasFocus());
}

void DisableIndicatorInkDrop(views::Button* button) {
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
}

}  // namespace

SeoulShellSpaceView::SeoulShellSpaceView(ShellController* controller) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetNotifyEnterExitOnChild(true);
  SetPreferredSize(
      gfx::Size(0, space_visuals::GetIndicatorHeight(/*collapsed=*/false)));
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(4, 12, 4, 6), 6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  std::unique_ptr<views::View> owned_glyph =
      std::make_unique<InteractionTrackingGlyphStack>(
          base::BindRepeating(&SeoulShellSpaceView::OnIndicatorPressed,
                              base::Unretained(this)),
          base::BindRepeating(
              &SeoulShellSpaceView::OnIndicatorIconDoubleClicked,
              base::Unretained(this)));
  indicator_glyph_ = owned_glyph.get();
  AddChildView(std::move(owned_glyph));
  indicator_glyph_->SetFocusBehavior(FocusBehavior::NEVER);
  indicator_glyph_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kStaticText);

  std::unique_ptr<views::Label> owned_name =
      std::make_unique<InteractionTrackingNameLabel>(
          base::BindRepeating(&SeoulShellSpaceView::OnIndicatorPressed,
                              base::Unretained(this)),
          base::BindRepeating(
              &SeoulShellSpaceView::OnIndicatorNameDoubleClicked,
              base::Unretained(this)));
  indicator_name_label_ = owned_name.get();
  AddChildView(std::move(owned_name));
  indicator_name_label_->SetFocusBehavior(FocusBehavior::NEVER);
  indicator_name_label_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kStaticText);

  std::unique_ptr<views::Textfield> owned_rename =
      std::make_unique<WorkspaceNameTextfield>(base::BindRepeating(
          &SeoulShellSpaceView::FinishRename, base::Unretained(this),
          /*commit=*/true));
  rename_field_ = owned_rename.get();
  AddChildView(std::move(owned_rename));
  rename_field_->SetController(this);
  rename_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 0)));
  rename_field_->SetFontList(rename_field_->GetFontList().DeriveWithWeight(
      gfx::Font::Weight::SEMIBOLD));
  rename_field_->SetAccessibleName(u"Space name");
  rename_field_->SetVisible(false);

  std::unique_ptr<views::ImageButton> owned_menu_button =
      std::make_unique<InteractionTrackingImageButton>(
          base::BindRepeating(&SeoulShellSpaceView::OnMenuPressed,
                              base::Unretained(this)),
          base::BindRepeating(&SeoulShellSpaceView::OnInteractionStateChanged,
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
      ui::ImageModel::FromVectorIcon(views::kOptionsIcon,
                                     kColorToolbarButtonIcon, 18));
  menu_button_->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(views::kOptionsIcon,
                                     kColorToolbarButtonIconHovered, 18));
  menu_button_->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(views::kOptionsIcon,
                                     kColorToolbarButtonIconPressed, 18));
  menu_button_->SetTooltipText(u"Space menu");
  menu_button_->GetViewAccessibility().SetName(u"Open Space menu");
  menu_button_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  menu_button_->GetViewAccessibility().SetIsCollapsed();
  menu_button_->SetPaintToLayer();
  menu_button_->layer()->SetFillsBoundsOpaquely(false);
  menu_button_->layer()->SetOpacity(0.0f);
  DisableIndicatorInkDrop(menu_button_);

  layout->SetFlexForView(indicator_name_label_, 1);
  layout->SetFlexForView(rename_field_, 1);
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
  if (renaming_) {
    FinishRename(/*commit=*/true);
  }
  presentation_collapsed_ = collapsed;
  auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  layout->set_inside_border_insets(collapsed ? gfx::Insets()
                                             : gfx::Insets::TLBR(4, 12, 4, 6));
  layout->set_main_axis_alignment(
      collapsed ? views::BoxLayout::MainAxisAlignment::kCenter
                : views::BoxLayout::MainAxisAlignment::kStart);
  SetPreferredSize(
      gfx::Size(0, space_visuals::GetIndicatorHeight(presentation_collapsed_)));
  indicator_name_label_->SetVisible(!collapsed);
  rename_field_->SetVisible(false);
  menu_button_->SetVisible(!collapsed);
  UpdateInteractionVisuals(/*animate=*/false);
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  PreferredSizeChanged();
}

void SeoulShellSpaceView::SetPinnedCollapsedChangedCallback(
    base::RepeatingCallback<void(bool)> callback) {
  pinned_collapsed_changed_callback_ = std::move(callback);
  if (pinned_collapsed_changed_callback_) {
    pinned_collapsed_changed_callback_.Run(pinned_collapsed_);
  }
}

std::u16string SeoulShellSpaceView::text_for_testing() const {
  return indicator_name_label_
             ? std::u16string(indicator_name_label_->GetText())
             : std::u16string();
}

void SeoulShellSpaceView::OnShellSnapshotChanged(
    const ShellChange& change,
    const ShellSnapshot& snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

void SeoulShellSpaceView::RebuildFromSnapshot(const ShellSnapshot& snapshot) {
  const size_t pinned_count = snapshot.pinned_items.size();
  const bool workspace_changed =
      rendered_workspace_id_.is_valid() &&
      rendered_workspace_id_ != snapshot.workspace.workspace_id;
  if (workspace_changed) {
    pinned_collapsed_ =
        pinned_collapsed_by_workspace_[snapshot.workspace.workspace_id];
    rendered_pinned_count_ = pinned_count;
    if (pinned_collapsed_changed_callback_) {
      pinned_collapsed_changed_callback_.Run(pinned_collapsed_);
    }
  }
  if (pinned_collapsed_ &&
      (pinned_count == 0 || pinned_count > rendered_pinned_count_)) {
    pinned_collapsed_ = false;
    pinned_collapsed_by_workspace_[snapshot.workspace.workspace_id] = false;
    if (pinned_collapsed_changed_callback_) {
      pinned_collapsed_changed_callback_.Run(false);
    }
  }
  rendered_pinned_count_ = pinned_count;
  const bool animate_switch = workspace_changed;
  rendered_workspace_id_ = snapshot.workspace.workspace_id;
  const std::u16string icon = ExplicitSpaceIcon(snapshot.workspace);
  const bool builtin_icon = IsWorkspaceBuiltinIcon(snapshot.workspace.icon);
  const std::u16string name = base::UTF8ToUTF16(snapshot.workspace.name);
  const std::u16string visible_icon =
      presentation_collapsed_ && !builtin_icon
          ? CollapsedSpaceLabel(snapshot.workspace)
          : icon;
  static_cast<InteractionTrackingGlyphStack*>(indicator_glyph_)
      ->SetWorkspaceIcon(builtin_icon ? snapshot.workspace.icon : std::string(),
                         visible_icon);
  indicator_name_label_->SetText(name.empty() ? u"Space" : name);
  indicator_name_label_->SetVisible(!presentation_collapsed_ && !renaming_);
  std::u16string accessible_name = u"Current Space";
  if (!name.empty()) {
    accessible_name += u", " + name;
  }
  if (snapshot.workspace.switching) {
    accessible_name += u", switching";
  }
  indicator_glyph_->GetViewAccessibility().SetName(accessible_name);
  indicator_glyph_->SetTooltipText(name.empty() ? u"Current Space" : name);
  indicator_name_label_->GetViewAccessibility().SetName(accessible_name);
  indicator_name_label_->SetTooltipText(name.empty() ? u"Current Space" : name);
  std::u16string menu_accessible_name = u"Open Space menu";
  if (!name.empty()) {
    menu_accessible_name += u", " + name;
  }
  menu_button_->GetViewAccessibility().SetName(menu_accessible_name);
  menu_button_->SetVisible(!presentation_collapsed_ && !renaming_);
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

void SeoulShellSpaceView::OnMenuPressed() {
  if (!controller_ || !GetWidget() || workspace_menu_open_) {
    return;
  }
  workspace_menu_open_ = true;
  menu_button_->GetViewAccessibility().SetIsExpanded();
  UpdateInteractionVisuals(/*animate=*/true);
  workspace_menu_ = std::make_unique<SeoulWorkspaceMenu>(
      GetWidget()->GetNativeWindow(), menu_button_, controller_);
  if (!workspace_menu_->Show(
          base::BindRepeating(&SeoulShellSpaceView::OnWorkspaceMenuClosed,
                              weak_factory_.GetWeakPtr()))) {
    OnWorkspaceMenuClosed();
  }
}

void SeoulShellSpaceView::OnIndicatorIconDoubleClicked() {
  if (!controller_ || !GetWidget() ||
      !controller_->snapshot().pinned_items.empty()) {
    return;
  }
  ShowWorkspaceIconPicker(indicator_glyph_, controller_,
                          controller_->snapshot().workspace.workspace_id);
}

void SeoulShellSpaceView::OnIndicatorNameDoubleClicked() {
  if (!controller_ || !controller_->snapshot().pinned_items.empty()) {
    return;
  }
  BeginRename();
}

void SeoulShellSpaceView::OnIndicatorPressed() {
  if (!controller_ || controller_->snapshot().pinned_items.empty()) {
    return;
  }
  pinned_collapsed_ = !pinned_collapsed_;
  pinned_collapsed_by_workspace_[controller_->snapshot()
                                     .workspace.workspace_id] =
      pinned_collapsed_;
  if (pinned_collapsed_changed_callback_) {
    pinned_collapsed_changed_callback_.Run(pinned_collapsed_);
  }
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::BeginRename() {
  if (renaming_ || presentation_collapsed_ || !controller_ || !GetWidget() ||
      !controller_->snapshot().pinned_items.empty()) {
    return;
  }
  renaming_ = true;
  rename_field_->SetText(
      base::UTF8ToUTF16(controller_->snapshot().workspace.name));
  indicator_name_label_->SetVisible(false);
  menu_button_->SetVisible(false);
  rename_field_->SetVisible(true);
  rename_field_->RequestFocus();
  rename_field_->SelectAll(false);
  InvalidateLayout();
}

void SeoulShellSpaceView::FinishRename(bool commit) {
  if (!renaming_) {
    return;
  }
  const std::u16string candidate(rename_field_->GetText());
  renaming_ = false;
  rename_field_->SetVisible(false);
  indicator_name_label_->SetVisible(!presentation_collapsed_);
  menu_button_->SetVisible(!presentation_collapsed_);

  if (commit && !candidate.empty() && controller_) {
    const std::string name = base::UTF16ToUTF8(candidate);
    if (name != controller_->snapshot().workspace.name) {
      BrowserCommand command;
      command.id = CommandId::Next();
      command.kind = CommandKind::kRenameWorkspace;
      command.workspace_id = controller_->snapshot().workspace.workspace_id;
      command.name = name;
      std::ignore = controller_->DispatchModelCommand(std::move(command));
    }
  }
  UpdateInteractionVisuals(/*animate=*/false);
  InvalidateLayout();
}

bool SeoulShellSpaceView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (sender != rename_field_ ||
      key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    FinishRename(/*commit=*/false);
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    FinishRename(/*commit=*/true);
    return true;
  }
  return false;
}

void SeoulShellSpaceView::OnIndicatorHoverChanged(bool hovered) {
  if (indicator_hovered_ == hovered) {
    return;
  }
  indicator_hovered_ = hovered;
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::OnInteractionStateChanged() {
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::OnWorkspaceMenuClosed() {
  if (!workspace_menu_open_) {
    return;
  }
  workspace_menu_open_ = false;
  menu_button_->GetViewAccessibility().SetIsCollapsed();
  UpdateInteractionVisuals(/*animate=*/true);
}

void SeoulShellSpaceView::UpdateInteractionVisuals(bool animate) {
  if (!indicator_glyph_ || !indicator_name_label_ || !menu_button_) {
    return;
  }
  const bool interacting =
      workspace_menu_open_ || indicator_hovered_ || IsInteracting(menu_button_);
  SetBackground(interacting && !presentation_collapsed_
                    ? views::CreateRoundedRectBackground(
                          kColorToolbarBackgroundSubtleEmphasis,
                          space_visuals::kSwitcherCornerRadius)
                    : nullptr);
  const bool has_pinned_tabs =
      controller_ && !controller_->snapshot().pinned_items.empty();
  static_cast<InteractionTrackingGlyphStack*>(indicator_glyph_)
      ->UpdateState(has_pinned_tabs, pinned_collapsed_, indicator_hovered_,
                    presentation_collapsed_, animate);

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

void SeoulShellSpaceView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  OnIndicatorHoverChanged(true);
}

void SeoulShellSpaceView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  OnIndicatorHoverChanged(false);
}

bool SeoulShellSpaceView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 1) {
    OnIndicatorPressed();
    return true;
  }
  return views::View::OnMousePressed(event);
}

BEGIN_METADATA(SeoulShellSpaceView)
END_METADATA

}  // namespace seoul
