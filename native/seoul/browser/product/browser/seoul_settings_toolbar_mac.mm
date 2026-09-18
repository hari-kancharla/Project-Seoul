// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#import <AppKit/AppKit.h>

#include "seoul/browser/product/browser/seoul_settings_toolbar_mac.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

@interface SeoulSettingsToolbarDelegate : NSObject <NSToolbarDelegate> {
@public
  base::RepeatingCallback<void(seoul::SettingsPane)> select_;
}
@property(nonatomic, readonly) NSArray<NSString *> *identifiers;
@end

@implementation SeoulSettingsToolbarDelegate
- (NSArray<NSString *> *)identifiers {
  return @[
    @"General", @"Appearance", @"Profiles", @"Shortcuts", @"Privacy",
    @"Advanced"
  ];
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:
    (NSToolbar *)toolbar {
  return self.identifiers;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:
    (NSToolbar *)toolbar {
  return self.identifiers;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarSelectableItemIdentifiers:
    (NSToolbar *)toolbar {
  return self.identifiers;
}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)inserted {
  const NSUInteger index = [self.identifiers indexOfObject:identifier];
  if (index == NSNotFound)
    return nil;
  NSArray<NSString *> *symbols = @[
    @"gearshape", @"rectangle.lefthalf.inset.filled", @"person", @"keyboard",
    @"hand.raised", @"slider.horizontal.3"
  ];
  NSToolbarItem *item =
      [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
  item.label = identifier;
  item.paletteLabel = identifier;
  item.toolTip = identifier;
  item.image = [NSImage imageWithSystemSymbolName:symbols[index]
                         accessibilityDescription:identifier];
  item.target = self;
  item.action = @selector(selectPane:);
  return item;
}
- (void)selectPane:(NSToolbarItem *)item {
  const NSUInteger index = [self.identifiers indexOfObject:item.itemIdentifier];
  if (index != NSNotFound && select_)
    select_.Run(static_cast<seoul::SettingsPane>(index));
}
@end

namespace seoul {
namespace {
class NativeSettingsToolbar final : public SettingsToolbarMac {
public:
  NativeSettingsToolbar(views::Widget *widget, SettingsPane pane,
                        base::RepeatingCallback<void(SettingsPane)> select)
      : window_(widget->GetNativeWindow().GetNativeNSWindow()),
        delegate_([[SeoulSettingsToolbarDelegate alloc] init]),
        toolbar_([[NSToolbar alloc] initWithIdentifier:@"SeoulSettings"]) {
    delegate_->select_ = std::move(select);
    toolbar_.delegate = delegate_;
    toolbar_.displayMode = NSToolbarDisplayModeIconAndLabel;
    toolbar_.allowsUserCustomization = NO;
    toolbar_.autosavesConfiguration = NO;
    window_.toolbarStyle = NSWindowToolbarStylePreference;
    window_.toolbar = toolbar_;
    [window_ standardWindowButton:NSWindowMiniaturizeButton].enabled = NO;
    [window_ standardWindowButton:NSWindowZoomButton].enabled = NO;
    Select(pane);
  }
  ~NativeSettingsToolbar() override {
    delegate_->select_.Reset();
    if (window_.toolbar == toolbar_)
      window_.toolbar = nil;
    toolbar_.delegate = nil;
  }
  void Select(SettingsPane pane) override {
    toolbar_.selectedItemIdentifier =
        delegate_.identifiers[static_cast<NSUInteger>(pane)];
  }
  bool ActivateForTesting(SettingsPane pane) override {
    NSString *identifier = delegate_.identifiers[static_cast<NSUInteger>(pane)];
    for (NSToolbarItem *item in toolbar_.items) {
      if ([item.itemIdentifier isEqualToString:identifier])
        return [NSApp sendAction:item.action to:item.target from:item];
    }
    return false;
  }

private:
  __weak NSWindow *window_;
  SeoulSettingsToolbarDelegate *__strong delegate_;
  NSToolbar *__strong toolbar_;
};
} // namespace

std::unique_ptr<SettingsToolbarMac>
CreateSettingsToolbarMac(views::Widget *widget, SettingsPane pane,
                         base::RepeatingCallback<void(SettingsPane)> select) {
  return std::make_unique<NativeSettingsToolbar>(widget, pane,
                                                 std::move(select));
}
} // namespace seoul
