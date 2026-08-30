// Project Seoul design-review capture support.
// Test-only: resolves the CGWindowID of a native window so the capture
// harness can hand it to macOS's own window snapshotter, which is the one
// path that reliably rasters a composited widget - shadow, sheet, and all -
// without ever seeing any other window.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_UTIL_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_UTIL_H_

#include <cstdint>

#include "ui/gfx/native_ui_types.h"

namespace seoul {

// Returns the CGWindowID for `window`, or 0 if it has none (not on screen).
uint64_t WindowNumberForCapture(gfx::NativeWindow window);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_UTIL_H_
