// Project Seoul design-review capture support.

#include "seoul/browser/product/browser/seoul_capture_util.h"

#import <AppKit/AppKit.h>

namespace seoul {

uint64_t WindowNumberForCapture(gfx::NativeWindow window) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  if (!ns_window) {
    return 0;
  }
  const NSInteger number = [ns_window windowNumber];
  return number > 0 ? static_cast<uint64_t>(number) : 0;
}

}  // namespace seoul
