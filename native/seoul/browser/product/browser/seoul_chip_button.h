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

  // Marks this chip one option of `total` in a single-choice row, at
  // 1-based `index`.
  //
  // Without it a chip is a plain button carrying a checked state, and on macOS
  // VoiceOver does not speak a value for a button role - so the current mode,
  // the one fact the row exists to state, is simply never announced. A radio
  // role with a set position is what produces "Balanced, selected, 2 of 3",
  // and it is what Chromium's own single-choice rows use.
  void SetChoice(int index, int total);

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
  // views::Button. The base implementation removes the checked state on every
  // transition that is not "pressed", so merely hovering the selected chip
  // erased the row's only authoritative state. This writes it from `selected_`
  // instead, which is where the truth actually lives.
  void UpdateAccessibleCheckedState() override;

  void UpdateBackground();

  bool selected_ = false;
  bool prominent_ = false;
  bool is_choice_ = false;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CHIP_BUTTON_H_
