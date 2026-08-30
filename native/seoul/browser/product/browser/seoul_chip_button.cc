// Project Seoul chip button.

#include "seoul/browser/product/browser/seoul_chip_button.h"

#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"

namespace seoul {

SeoulChipButton::SeoulChipButton(views::Button::PressedCallback callback,
                                 std::u16string text)
    : views::LabelButton(std::move(callback), std::move(text)) {
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
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

void SeoulChipButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateBackground();
  SchedulePaint();
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

void SeoulChipButton::UpdateBackground() {
  const bool highlighted = selected_ || prominent_ ||
                           GetState() == views::Button::STATE_HOVERED ||
                           GetState() == views::Button::STATE_PRESSED;
  SetBackground(highlighted
                    ? views::CreateRoundedRectBackground(
                          kColorToolbarBackgroundSubtleEmphasis,
                          kSeoulChipCornerRadius)
                    : nullptr);
}

BEGIN_METADATA(SeoulChipButton)
END_METADATA

}  // namespace seoul
