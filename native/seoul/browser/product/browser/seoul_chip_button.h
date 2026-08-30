// Project Seoul chip button.
// The one pill-shaped control Seoul's native panels build from: a rounded
// highlight on hover and press (as an ink ripple), a persistent highlight
// while selected, and an optional resting background for chips that perform
// an action rather than record a choice. One class, so every panel's chips
// round, pad, and answer the pointer identically.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CHIP_BUTTON_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CHIP_BUTTON_H_

#include <string>

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"

namespace seoul {

// Corner radius shared by every Seoul chip - panels must not drift into
// slightly different pill shapes for the same control.
inline constexpr int kSeoulChipCornerRadius = 6;

class SeoulChipButton final : public views::LabelButton {
  METADATA_HEADER(SeoulChipButton, views::LabelButton)

 public:
  SeoulChipButton(views::Button::PressedCallback callback, std::u16string text);
  SeoulChipButton(const SeoulChipButton&) = delete;
  SeoulChipButton& operator=(const SeoulChipButton&) = delete;
  ~SeoulChipButton() override;

  // Set once from authoritative state read-back, not on every click, so a
  // chip whose write fails silently does not claim to be selected.
  void SetSelected(bool selected);

  // An action chip - a stepper, a reset, a footer verb - keeps a resting
  // background: it performs rather than selects, and with nothing selected
  // it must still read as pressable. Choice chips stay quiet at rest so the
  // one persistent highlight always means "this is the current choice".
  void SetProminent(bool prominent);

 private:
  // views::LabelButton:
  void StateChanged(ButtonState old_state) override;

  void UpdateBackground();

  bool selected_ = false;
  bool prominent_ = false;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CHIP_BUTTON_H_
