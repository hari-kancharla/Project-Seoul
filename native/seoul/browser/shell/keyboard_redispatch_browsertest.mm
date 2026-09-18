// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#import <AppKit/AppKit.h>

#include "base/apple/scoped_objc_class_swizzler.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "ui/base/cocoa/command_dispatcher.h"

namespace {
CommandDispatcher* __weak g_dispatcher;
int g_native_dispatches = 0;
bool g_reenter = true;
bool g_nested_handled = true;
}  // namespace

// Reproduce AppKit returning an unhandled event to the browser before its
// original native dispatch has unwound. Bound the recursion so a regression
// produces a test failure instead of exhausting the process stack.
@interface SeoulReentrantKeyApplication : NSObject
- (void)sendEvent:(NSEvent*)event;
@end

@implementation SeoulReentrantKeyApplication
- (void)sendEvent:(NSEvent*)event {
  ++g_native_dispatches;
  if (g_reenter && g_native_dispatches < 8) {
    g_nested_handled = [g_dispatcher redispatchKeyEvent:event];
  }
}
@end

class SeoulKeyboardBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(SeoulKeyboardBrowserTest,
                       NativeRedispatchCannotReenterTheSameWindow) {
  NSWindow<CommandDispatchingWindow>* window =
      (NSWindow<CommandDispatchingWindow>*)browser()
          ->window()
          ->GetNativeWindow()
          .GetNativeNSWindow();
  ASSERT_TRUE([window conformsToProtocol:@protocol(CommandDispatchingWindow)]);
  g_dispatcher = [window commandDispatcher];
  ASSERT_TRUE(g_dispatcher);
  base::apple::ScopedObjCClassSwizzler swizzler(
      [NSApp class], [SeoulReentrantKeyApplication class],
      @selector(sendEvent:));
  for (NSEventType type :
       {NSEventTypeKeyDown, NSEventTypeKeyUp, NSEventTypeFlagsChanged}) {
    NSEvent* event = [NSEvent keyEventWithType:type
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:window.windowNumber
                                       context:nil
                                    characters:@"x"
                   charactersIgnoringModifiers:@"x"
                                     isARepeat:NO
                                       keyCode:7];
    g_native_dispatches = 0;
    g_nested_handled = true;
    g_reenter = true;
    [g_dispatcher redispatchKeyEvent:event];
    EXPECT_EQ(1, g_native_dispatches);
    EXPECT_FALSE(g_nested_handled);

    // Once the original dispatch returns, the next key must still reach AppKit.
    g_reenter = false;
    [g_dispatcher redispatchKeyEvent:event];
    EXPECT_EQ(2, g_native_dispatches);
  }
  g_dispatcher = nil;
}
