// Project Seoul chip button.

#include "seoul/browser/product/browser/seoul_chip_button.h"

#include <algorithm>
#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace seoul {

namespace {
class FontSelectionBackground final : public views::Background {
 public:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(view->GetColorProvider()->GetColor(
        kColorToolbarBackgroundSubtleEmphasis));
    canvas->DrawCircle(gfx::PointF(view->GetLocalBounds().CenterPoint()),
                       std::min(view->width(), view->height()) / 2.f, flags);
  }
};
}  // namespace

SeoulChipButton::SeoulChipButton(views::Button::PressedCallback callback,
                                 std::u16string text)
    : views::LabelButton(std::move(callback), std::move(text)) {
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
  SetMinSize(gfx::Size(28, 28));
  SetFocusRingCornerRadius(kSeoulChipCornerRadius);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::UseInkDropForFloodFillRipple(views::InkDrop::Get(this),
                                               /*highlight_on_hover=*/true,
                                               /*highlight_on_focus=*/false);
  views::InkDrop::Get(this)->SetBaseColor(kColorToolbarButtonIcon);
  views::InkDrop::Get(this)->SetVisibleOpacity(0.10f);
  views::InkDrop::Get(this)->SetHighlightOpacity(0.08f);
  UpdateBackground();
}

SeoulChipButton::~SeoulChipButton() = default;

void SeoulChipButton::SetPreviewFont(const gfx::FontList& font) {
  font_preview_ = true;
  label()->SetFontList(font);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(3, 3)));
  SetFocusRingCornerRadius(16);
  UpdateBackground();
  PreferredSizeChanged();
}

void SeoulChipButton::SetChoice(int index, int total) {
  is_choice_ = true;
  GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
  GetViewAccessibility().SetPosInSet(index);
  GetViewAccessibility().SetSetSize(total);
  // One tab stop for the row, arrow keys within it, which is how a
  // single-choice group is expected to behave.
  SetGroup(total);
  UpdateAccessibleCheckedState();
}

void SeoulChipButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateAccessibleCheckedState();
  UpdateBackground();
  SchedulePaint();
}

void SeoulChipButton::UpdateAccessibleCheckedState() {
  if (!is_choice_) {
    views::LabelButton::UpdateAccessibleCheckedState();
    return;
  }
  // Always from `selected_`, and always present: a row where the unselected
  // options carry no state at all says only what is chosen, never what is not.
  GetViewAccessibility().SetCheckedState(selected_
                                             ? ax::mojom::CheckedState::kTrue
                                             : ax::mojom::CheckedState::kFalse);
}

void SeoulChipButton::SetProminent(bool prominent) {
  if (prominent_ == prominent) {
    return;
  }
  prominent_ = prominent;
  UpdateBackground();
}

void SeoulChipButton::StateChanged(ButtonState old_state) {
  views::LabelButton::StateChanged(old_state);
  UpdateBackground();
}

bool SeoulChipButton::OnKeyPressed(const ui::KeyEvent& event) {
  if (!font_preview_ || !GetWidget())
    return views::LabelButton::OnKeyPressed(event);
  int offset = 0;
  switch (event.key_code()) {
    case ui::VKEY_LEFT:
      offset = -1;
      break;
    case ui::VKEY_RIGHT:
      offset = 1;
      break;
    case ui::VKEY_UP:
      offset = -5;
      break;
    case ui::VKEY_DOWN:
      offset = 5;
      break;
    default:
      return views::LabelButton::OnKeyPressed(event);
  }
  views::View::Views choices;
  GetWidget()->GetContentsView()->GetViewsInGroup(GetGroup(), &choices);
  const auto current = std::ranges::find(choices, this);
  if (current == choices.end() || choices.empty())
    return false;
  const int count = static_cast<int>(choices.size());
  auto* next = static_cast<SeoulChipButton*>(
      choices[(static_cast<int>(current - choices.begin()) + offset + count) %
              count]);
  next->RequestFocus();
  next->NotifyClick(event);
  return true;
}

bool SeoulChipButton::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  if (font_preview_ &&
      (event.key_code() == ui::VKEY_LEFT ||
       event.key_code() == ui::VKEY_RIGHT || event.key_code() == ui::VKEY_UP ||
       event.key_code() == ui::VKEY_DOWN)) {
    return true;
  }
  return views::LabelButton::SkipDefaultKeyEventProcessing(event);
}

void SeoulChipButton::UpdateBackground() {
  if (font_preview_) {
    SetBackground(selected_ ? std::make_unique<FontSelectionBackground>()
                            : nullptr);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(3, 3)));
    return;
  }
  // Three meanings, three treatments. They used to share one fill, so a
  // selected choice, a pressable action and a hovered chip were
  // indistinguishable - and a fill that light carries far too little contrast
  // to be the only signal that a setting is active.
  const bool transient = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
  if (prominent_) {
    // Keep actions neutral, matching the browser's other site controls.
    // Only a selected choice receives the persistent outline below.
    SetBackground(views::CreateRoundedRectBackground(
        kColorToolbarBackgroundSubtleEmphasis, kSeoulChipCornerRadius));
  } else {
    SetBackground(
        transient || selected_
            ? views::CreateRoundedRectBackground(
                  kColorToolbarBackgroundSubtleEmphasis, kSeoulChipCornerRadius)
            : nullptr);
  }

  // Selection also carries a border, so it survives being read without colour
  // and does not depend on a fill a few percent off the panel behind it. The
  // padded border keeps the chip's outer metrics identical either way, so
  // selecting a chip cannot reflow the row.
  if (selected_ && !prominent_) {
    SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, kSeoulChipCornerRadius,
                                       kColorToolbarButtonIcon),
        gfx::Insets::VH(3, 9)));
  } else {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
  }
}

BEGIN_METADATA(SeoulChipButton)
END_METADATA

}  // namespace seoul
