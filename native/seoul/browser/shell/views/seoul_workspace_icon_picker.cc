// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "seoul/browser/shell/views/seoul_workspace_icon_picker.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/workspace_emoji_data.h"
#include "seoul/browser/shell/workspace_icon_data.h"
#include "seoul/browser/shell/workspace_icon_painter.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kPickerWidth = 250;
constexpr int kPickerPagesHeight = 230;
constexpr int kPickerHeaderHeight = 34;
constexpr int kEmojiSearchHeight = 34;
constexpr int kGridColumns = 7;
constexpr int kChoiceSize = 22;
constexpr int kChoiceColumnWidth = 28;
constexpr int kChoiceGap = 5;
constexpr int kChoiceCornerRadius = 4;
constexpr int kGridHorizontalInset = 10;
constexpr int kGridTopInset = 5;

enum class PickerPage {
  kEmoji,
  kBuiltin,
};

class WorkspaceIconChoiceButton final : public views::LabelButton {
  METADATA_HEADER(WorkspaceIconChoiceButton, views::LabelButton)

 public:
  WorkspaceIconChoiceButton(views::Button::PressedCallback callback,
                            std::string icon_ref,
                            std::u16string accessible_name,
                            bool builtin)
      : views::LabelButton(
            std::move(callback),
            builtin ? std::u16string() : base::UTF8ToUTF16(icon_ref)),
        icon_ref_(std::move(icon_ref)),
        builtin_(builtin) {
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetPreferredSize(gfx::Size(kChoiceSize, kChoiceSize));
    SetMinSize(gfx::Size(kChoiceSize, kChoiceSize));
    SetMaxSize(gfx::Size(kChoiceSize, kChoiceSize));
    SetBorder(views::CreateEmptyBorder(gfx::Insets(4)));
    SetEnabledTextColors(kColorToolbarText);
    SetFocusRingCornerRadius(kChoiceCornerRadius);
    SetTextSubpixelRenderingEnabled(false);
    label()->SetFontList(label()->font_list().DeriveWithSizeDelta(
        14 - label()->font_list().GetFontSize()));
    GetViewAccessibility().SetName(std::move(accessible_name));
    SetTooltipText(GetViewAccessibility().GetCachedName());
    UpdateBackground();
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateBackground();
    SchedulePaint();
  }

 private:
  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateBackground();
  }

  void UpdateBackground() {
    const bool highlighted = selected_ ||
                             GetState() == views::Button::STATE_HOVERED ||
                             GetState() == views::Button::STATE_PRESSED;
    SetBackground(highlighted ? views::CreateRoundedRectBackground(
                                    kColorToolbarBackgroundSubtleEmphasis,
                                    kChoiceCornerRadius)
                              : nullptr);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!builtin_ || !GetColorProvider()) {
      views::LabelButton::PaintButtonContents(canvas);
      return;
    }
    ui::ColorId color_id = kColorToolbarButtonIcon;
    if (GetState() == views::Button::STATE_HOVERED) {
      color_id = kColorToolbarButtonIconHovered;
    } else if (GetState() == views::Button::STATE_PRESSED) {
      color_id = kColorToolbarButtonIconPressed;
    } else if (GetState() == views::Button::STATE_DISABLED) {
      color_id = kColorToolbarButtonIconDisabled;
    }
    gfx::Rect icon_bounds = GetLocalBounds();
    icon_bounds.ClampToCenteredSize(gfx::Size(14, 14));
    PaintWorkspaceBuiltinIcon(canvas, icon_ref_, icon_bounds,
                              GetColorProvider()->GetColor(color_id));
  }

  std::string icon_ref_;
  bool builtin_ = false;
  bool selected_ = false;
};

class WorkspaceIconGrid final : public views::View {
  METADATA_HEADER(WorkspaceIconGrid, views::View)

 public:
  WorkspaceIconGrid() = default;

  void ContentsChanged() {
    PreferredSizeChanged();
    InvalidateLayout();
  }

 private:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    int visible = 0;
    for (const views::View* child : children()) {
      visible += child->GetVisible() ? 1 : 0;
    }
    const int rows = std::max(1, (visible + kGridColumns - 1) / kGridColumns);
    return gfx::Size(
        2 * kGridHorizontalInset + kGridColumns * kChoiceColumnWidth +
            (kGridColumns - 1) * kChoiceGap,
        kGridTopInset + rows * kChoiceSize + (rows - 1) * kChoiceGap);
  }

  void Layout(PassKey) override {
    int visible_index = 0;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      const int column = visible_index % kGridColumns;
      const int row = visible_index / kGridColumns;
      child->SetBounds(kGridHorizontalInset +
                           column * (kChoiceColumnWidth + kChoiceGap) +
                           (kChoiceColumnWidth - kChoiceSize) / 2,
                       kGridTopInset + row * (kChoiceSize + kChoiceGap),
                       kChoiceSize, kChoiceSize);
      ++visible_index;
    }
  }
};

class SeoulWorkspaceIconPicker final : public views::View,
                                       public views::TextfieldController {
  METADATA_HEADER(SeoulWorkspaceIconPicker, views::View)

 public:
  SeoulWorkspaceIconPicker(ShellController* controller,
                           WorkspaceId workspace_id)
      : controller_(controller), workspace_id_(workspace_id) {
    SetPreferredSize(
        gfx::Size(kPickerWidth, kPickerPagesHeight + kPickerHeaderHeight));
    if (controller_ && controller_->model()) {
      if (const WorkspaceRecord* workspace =
              controller_->model()->FindWorkspace(workspace_id_)) {
        current_icon_ = workspace->icon;
      }
    }
    BuildContents();
  }

  ~SeoulWorkspaceIconPicker() override {
    if (active_picker_ == this) {
      active_picker_ = nullptr;
    }
  }

  static void Show(views::View* anchor,
                   ShellController* controller,
                   WorkspaceId workspace_id) {
    if (!anchor || !anchor->GetWidget() || !controller ||
        !workspace_id.is_valid()) {
      return;
    }
    if (active_picker_ && active_picker_->GetWidget()) {
      active_picker_->GetWidget()->Close();
    }
    auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::TOP_RIGHT);
    bubble_delegate->SetAccessibleTitle(u"Change Space Icon");
    bubble_delegate->SetShowTitle(false);
    bubble_delegate->SetShowCloseButton(false);
    bubble_delegate->SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    bubble_delegate->set_close_on_deactivate(true);
    bubble_delegate->set_margins(gfx::Insets());
    auto picker =
        std::make_unique<SeoulWorkspaceIconPicker>(controller, workspace_id);
    SeoulWorkspaceIconPicker* picker_ptr = picker.get();
    bubble_delegate->SetContentsView(std::move(picker));
    views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
        std::move(bubble_delegate),
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
    active_picker_ = picker_ptr;
    widget->Show();
  }

  void BuildContents() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* switcher = AddChildView(std::make_unique<views::View>());
    switcher->SetPreferredSize(gfx::Size(0, kPickerHeaderHeight));
    auto* switcher_layout =
        switcher->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(6, 16, 6, 16), 4));
    switcher_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* leading_placeholder =
        switcher->AddChildView(std::make_unique<views::View>());
    leading_placeholder->SetPreferredSize(gfx::Size(kChoiceSize, kChoiceSize));
    auto* leading_spacer =
        switcher->AddChildView(std::make_unique<views::View>());
    switcher_layout->SetFlexForView(leading_spacer, 1);
    auto* page_buttons =
        switcher->AddChildView(std::make_unique<views::View>());
    auto* page_buttons_layout =
        page_buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    page_buttons_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    emoji_page_button_ =
        page_buttons->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SeoulWorkspaceIconPicker::SetPage,
                                base::Unretained(this), PickerPage::kEmoji),
            u"Emoji"));
    builtin_page_button_ =
        page_buttons->AddChildView(std::make_unique<views::LabelButton>(
            base::BindRepeating(&SeoulWorkspaceIconPicker::SetPage,
                                base::Unretained(this), PickerPage::kBuiltin),
            u"Icons"));
    auto* trailing_spacer =
        switcher->AddChildView(std::make_unique<views::View>());
    switcher_layout->SetFlexForView(trailing_spacer, 1);
    none_button_ =
        switcher->AddChildView(std::make_unique<WorkspaceIconChoiceButton>(
            base::BindRepeating(&SeoulWorkspaceIconPicker::SelectIcon,
                                base::Unretained(this), std::string()),
            WorkspaceBuiltinIconRef("trash"), u"Remove Space icon",
            /*builtin=*/true));
    none_button_->SetPaintToLayer();
    none_button_->layer()->SetFillsBoundsOpaquely(false);
    for (views::LabelButton* button :
         {emoji_page_button_, builtin_page_button_}) {
      button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(3, 6)));
      button->SetFocusRingCornerRadius(kChoiceCornerRadius);
      button->SetLabelStyle(views::style::STYLE_EMPHASIZED);
    }

    auto search_row = std::make_unique<views::View>();
    search_row_ = search_row.get();
    AddChildView(std::move(search_row));
    search_row_->SetPreferredSize(gfx::Size(0, kEmojiSearchHeight));
    auto* search_layout =
        search_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(0, 10, 6, 10), 0));
    search_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    search_ = search_row_->AddChildView(std::make_unique<views::Textfield>());
    search_->SetPlaceholderText(u"Search emojis");
    search_->SetAccessibleName(u"Search Space emojis");
    search_->SetController(this);
    search_layout->SetFlexForView(search_, 1);

    auto grid = std::make_unique<WorkspaceIconGrid>();
    grid_ = grid.get();
    grid_->SetPaintToLayer();
    grid_->layer()->SetFillsBoundsOpaquely(false);
    BuildChoices();

    scroll_ = AddChildView(std::make_unique<views::ScrollView>());
    scroll_->SetDrawOverflowIndicator(false);
    scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll_->ClipHeightTo(kPickerPagesHeight - kEmojiSearchHeight,
                          kPickerPagesHeight - kEmojiSearchHeight);
    scroll_->SetContents(std::move(grid));
    layout->SetFlexForView(scroll_, 1);

    SetPage(PickerPage::kEmoji);
    UpdateSelection();
  }

  void AddedToWidget() override {
    views::View::AddedToWidget();
    if (search_) {
      search_->RequestFocus();
    }
  }

  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    if (sender != search_) {
      return;
    }
    if (page_ != PickerPage::kEmoji) {
      return;
    }
    ApplyFilter(base::ToLowerASCII(base::UTF16ToUTF8(new_contents)));
  }

 private:
  struct ChoiceBinding {
    raw_ptr<WorkspaceIconChoiceButton> button;
    PickerPage page;
    std::string_view search_terms;
    std::string icon_ref;
  };

  void BuildChoices() {
    for (const WorkspaceEmojiData& emoji : WorkspaceEmojiCatalog()) {
      const std::string icon_ref(emoji.emoji);
      auto* button =
          grid_->AddChildView(std::make_unique<WorkspaceIconChoiceButton>(
              base::BindRepeating(&SeoulWorkspaceIconPicker::SelectIcon,
                                  base::Unretained(this), icon_ref),
              icon_ref, base::UTF8ToUTF16(icon_ref), /*builtin=*/false));
      choices_.push_back(
          {button, PickerPage::kEmoji, emoji.search_terms, icon_ref});
    }
    for (const WorkspaceBuiltinIconData& icon : WorkspaceBuiltinIconCatalog()) {
      const std::string icon_ref = WorkspaceBuiltinIconRef(icon.name);
      auto* button =
          grid_->AddChildView(std::make_unique<WorkspaceIconChoiceButton>(
              base::BindRepeating(&SeoulWorkspaceIconPicker::SelectIcon,
                                  base::Unretained(this), icon_ref),
              icon_ref, base::UTF8ToUTF16(icon.name), /*builtin=*/true));
      choices_.push_back({button, PickerPage::kBuiltin, icon.name, icon_ref});
    }
  }

  void SetPage(PickerPage page) {
    const bool animate = page_initialized_ && page_ != page;
    const bool forward = page == PickerPage::kBuiltin;
    page_ = page;
    if (search_row_) {
      search_row_->SetVisible(page == PickerPage::kEmoji);
    }
    if (scroll_) {
      const int scroll_height = page == PickerPage::kEmoji
                                    ? kPickerPagesHeight - kEmojiSearchHeight
                                    : kPickerPagesHeight;
      scroll_->ClipHeightTo(scroll_height, scroll_height);
    }
    if (emoji_page_button_ && builtin_page_button_) {
      emoji_page_button_->SetBackground(
          page == PickerPage::kEmoji
              ? views::CreateRoundedRectBackground(
                    kColorToolbarBackgroundSubtleEmphasis, kChoiceCornerRadius)
              : nullptr);
      builtin_page_button_->SetBackground(
          page == PickerPage::kBuiltin
              ? views::CreateRoundedRectBackground(
                    kColorToolbarBackgroundSubtleEmphasis, kChoiceCornerRadius)
              : nullptr);
    }
    ApplyFilter(page == PickerPage::kEmoji && search_
                    ? base::ToLowerASCII(base::UTF16ToUTF8(search_->GetText()))
                    : std::string());
    page_initialized_ = true;
    if (animate && grid_ && grid_->layer()) {
      gfx::Transform start;
      start.Translate(forward ? 16.0f : -16.0f, 0.0f);
      grid_->layer()->GetAnimator()->StopAnimating();
      grid_->layer()->SetTransform(start);
      grid_->layer()->SetOpacity(0.6f);
      views::AnimationBuilder()
          .SetPreemptionStrategy(
              ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
          .Once()
          .SetDuration(base::Milliseconds(180))
          .SetTransform(grid_->layer(), gfx::Transform(), gfx::Tween::EASE_OUT)
          .SetOpacity(grid_->layer(), 1.0f, gfx::Tween::EASE_OUT);
    }
  }

  void ApplyFilter(std::string query) {
    for (ChoiceBinding& choice : choices_) {
      const bool page_matches = choice.page == page_;
      const bool query_matches =
          query.empty() ||
          choice.search_terms.find(query) != std::string::npos ||
          choice.icon_ref.find(query) != std::string::npos;
      choice.button->SetVisible(page_matches && query_matches);
    }
    if (grid_) {
      grid_->ContentsChanged();
    }
    if (scroll_) {
      scroll_->ScrollToOffset(gfx::PointF());
    }
  }

  void SelectIcon(std::string icon_ref) {
    if (!controller_ || icon_ref == current_icon_) {
      return;
    }
    BrowserCommand command;
    command.id = CommandId::Next();
    command.kind = CommandKind::kSetWorkspaceIcon;
    command.workspace_id = workspace_id_;
    command.icon = icon_ref;
    if (!controller_->DispatchModelCommand(std::move(command)).has_value()) {
      return;
    }
    current_icon_ = std::move(icon_ref);
    UpdateSelection();
  }

  void UpdateSelection() {
    for (ChoiceBinding& choice : choices_) {
      choice.button->SetSelected(choice.icon_ref == current_icon_);
    }
    if (none_button_) {
      none_button_->SetEnabled(!current_icon_.empty());
      none_button_->layer()->SetOpacity(current_icon_.empty() ? 0.0f : 1.0f);
    }
  }

  static raw_ptr<SeoulWorkspaceIconPicker> active_picker_;

  raw_ptr<ShellController> controller_;
  WorkspaceId workspace_id_;
  std::string current_icon_;
  PickerPage page_ = PickerPage::kEmoji;
  bool page_initialized_ = false;
  raw_ptr<views::View> search_row_ = nullptr;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::LabelButton> emoji_page_button_ = nullptr;
  raw_ptr<views::LabelButton> builtin_page_button_ = nullptr;
  raw_ptr<WorkspaceIconChoiceButton> none_button_ = nullptr;
  raw_ptr<WorkspaceIconGrid> grid_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::vector<ChoiceBinding> choices_;
};

raw_ptr<SeoulWorkspaceIconPicker> SeoulWorkspaceIconPicker::active_picker_ =
    nullptr;

BEGIN_METADATA(WorkspaceIconChoiceButton)
END_METADATA

BEGIN_METADATA(WorkspaceIconGrid)
END_METADATA

BEGIN_METADATA(SeoulWorkspaceIconPicker)
END_METADATA

}  // namespace

void ShowWorkspaceIconPicker(views::View* anchor,
                             ShellController* controller,
                             WorkspaceId workspace_id) {
  SeoulWorkspaceIconPicker::Show(anchor, controller, workspace_id);
}

}  // namespace seoul
