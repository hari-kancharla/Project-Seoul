// Project Seoul outbound browser command layer.

#include "seoul/browser/commands/chromium_mutation_adapter_impl.h"

#include <optional>
#include <vector>

#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/platform_util.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/browser_tabstrip.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_handle.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "ui/base/page_transition_types.h"

namespace seoul {
namespace {

BrowserWindowInterface* FindBrowser(Profile* profile, LiveWindowKey window) {
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  if (!collection) {
    return nullptr;
  }
  const SessionID desired = SessionID::FromSerializedValue(window.session_id());
  BrowserWindowInterface* browser = collection->FindBrowserWithID(desired);
  if (!browser || browser->GetProfile() != profile ||
      browser->IsDeleteScheduled()) {
    return nullptr;
  }
  return browser;
}

}  // namespace

ChromiumMutationAdapterImpl::ChromiumMutationAdapterImpl() = default;
ChromiumMutationAdapterImpl::~ChromiumMutationAdapterImpl() = default;

CommandStatusResult ChromiumMutationAdapterImpl::OpenNewTab(
    Profile* profile,
    const ResolvedWindowTarget& window,
    CommandForegroundDisposition disposition,
    LiveTabKey* out_tab) {
  BrowserWindowInterface* browser = FindBrowser(profile, window.window);
  if (!browser) {
    return CommandErr(CommandError::kWindowNotFound);
  }
  const bool foreground =
      disposition == CommandForegroundDisposition::kForeground;
  // An empty GURL loads Chromium's normal New Tab Page (the documented behavior
  // of AddAndReturnTabAt). This is the dedicated new-tab path and never goes
  // through URL navigation or URL policy.
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser, GURL(), /*index=*/-1, foreground);
  if (!contents) {
    return CommandErr(CommandError::kDispatchFailure);
  }
  if (out_tab) {
    *out_tab = TabStripBridge::KeyForContents(contents);
  }
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::OpenTab(
    Profile* profile,
    const ResolvedWindowTarget& window,
    const GURL& url,
    CommandForegroundDisposition disposition,
    LiveTabKey* out_tab) {
  BrowserWindowInterface* browser = FindBrowser(profile, window.window);
  if (!browser) {
    return CommandErr(CommandError::kWindowNotFound);
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = disposition == CommandForegroundDisposition::kForeground
                           ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                           : WindowOpenDisposition::NEW_BACKGROUND_TAB;
  base::WeakPtr<content::NavigationHandle> handle = Navigate(&params);
  if (!handle && !params.navigated_or_inserted_contents) {
    return CommandErr(CommandError::kDispatchFailure);
  }
  if (params.navigated_or_inserted_contents && out_tab) {
    *out_tab =
        TabStripBridge::KeyForContents(params.navigated_or_inserted_contents);
  }
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::NavigateTab(
    Profile* profile,
    const ResolvedTabTarget& tab,
    const GURL& url) {
  BrowserWindowInterface* browser = FindBrowser(profile, tab.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(tab.current_index)) {
    return CommandErr(CommandError::kTargetDisappeared);
  }
  content::WebContents* contents =
      strip->GetWebContentsAt(tab.current_index);
  if (!contents || TabStripBridge::KeyForContents(contents) != tab.tab) {
    return CommandErr(CommandError::kStaleTabReference);
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  params.source_contents = contents;
  params.user_gesture = true;
  params.original_user_gesture = true;
  base::WeakPtr<content::NavigationHandle> handle = Navigate(&params);
  return handle || params.navigated_or_inserted_contents
             ? CommandOk()
             : CommandErr(CommandError::kDispatchFailure);
}

CommandStatusResult ChromiumMutationAdapterImpl::OpenTabInSplit(
    Profile* profile,
    const ResolvedWindowTarget& window,
    LiveTabKey existing_tab,
    const GURL& url,
    double ratio,
    LiveTabKey* out_tab,
    std::string* upstream_token) {
  BrowserWindowInterface* browser = FindBrowser(profile, window.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !existing_tab.is_valid()) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  auto find_index = [&](LiveTabKey key) {
    for (int index = 0; index < strip->count(); ++index) {
      if (TabStripBridge::KeyForTab(strip->GetTabAtIndex(index)) == key) {
        return index;
      }
    }
    return -1;
  };
  const int existing_index = find_index(existing_tab);
  if (!strip->ContainsIndex(existing_index) ||
      strip->GetTabAtIndex(existing_index)->IsSplit()) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  LiveTabKey inserted;
  CommandStatusResult opened =
      OpenTab(profile, window, url, CommandForegroundDisposition::kForeground,
              &inserted);
  if (!opened.has_value() || !inserted.is_valid()) {
    return opened;
  }
  ResolvedSplitTarget split;
  split.window = window.window;
  split.pane_a = existing_tab;
  split.pane_b = inserted;
  split.pane_a_index = find_index(existing_tab);
  split.pane_b_index = find_index(inserted);
  CommandStatusResult created =
      CreateSplit(profile, split, ratio, upstream_token);
  if (!created.has_value()) {
    const int inserted_index = find_index(inserted);
    if (strip->ContainsIndex(inserted_index)) {
      strip->CloseWebContentsAt(inserted_index, TabCloseTypes::CLOSE_NONE);
    }
    return created;
  }
  if (out_tab) {
    *out_tab = inserted;
  }
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::OpenExternal(
    Profile* profile,
    const GURL& url) {
  if (!profile || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return CommandErr(CommandError::kInvalidUrl);
  }
#if BUILDFLAG(IS_CHROMEOS)
  platform_util::OpenExternal(profile, url);
#else
  platform_util::OpenExternal(url);
#endif
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::ActivateTab(
    Profile* profile,
    const ResolvedTabTarget& tab) {
  BrowserWindowInterface* browser = FindBrowser(profile, tab.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(tab.current_index)) {
    return CommandErr(CommandError::kTargetDisappeared);
  }
  strip->ActivateTabAt(tab.current_index);
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::CloseTab(
    Profile* profile,
    const ResolvedTabTarget& tab) {
  BrowserWindowInterface* browser = FindBrowser(profile, tab.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(tab.current_index)) {
    return CommandErr(CommandError::kTargetDisappeared);
  }
  strip->CloseWebContentsAt(tab.current_index,
                            TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB |
                                TabCloseTypes::CLOSE_USER_GESTURE);
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::SetPinned(
    Profile* profile,
    const ResolvedTabTarget& tab,
    bool pinned) {
  BrowserWindowInterface* browser = FindBrowser(profile, tab.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(tab.current_index)) {
    return CommandErr(CommandError::kTargetDisappeared);
  }
  strip->SetTabPinned(tab.current_index, pinned);
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::MoveTab(
    Profile* profile,
    const ResolvedTabTarget& tab,
    int destination_index) {
  BrowserWindowInterface* browser = FindBrowser(profile, tab.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || !strip->ContainsIndex(tab.current_index) ||
      destination_index < 0 || destination_index >= strip->count()) {
    return CommandErr(CommandError::kInvalidDestination);
  }
  strip->MoveWebContentsAt(tab.current_index, destination_index,
                           /*select_after_move=*/false);
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::CreateSplit(
    Profile* profile,
    const ResolvedSplitTarget& split,
    double ratio,
    std::string* upstream_token) {
  BrowserWindowInterface* browser = FindBrowser(profile, split.window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }

  auto resolve_index = [&](LiveTabKey key) -> int {
    for (int index = 0; index < strip->count(); ++index) {
      if (TabStripBridge::KeyForTab(strip->GetTabAtIndex(index)) == key) {
        return index;
      }
    }
    return -1;
  };

  int index_a = split.pane_a_index;
  int index_b = split.pane_b_index;
  if (index_a < 0 || index_b < 0 || index_a == index_b) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  if (!strip->ContainsIndex(index_a) || !strip->ContainsIndex(index_b)) {
    return CommandErr(CommandError::kTargetDisappeared);
  }
  tabs::TabInterface* tab_a = strip->GetTabAtIndex(index_a);
  tabs::TabInterface* tab_b = strip->GetTabAtIndex(index_b);
  if (!tab_a || !tab_b || tab_a->IsSplit() || tab_b->IsSplit()) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  if (TabStripBridge::KeyForTab(tab_a) != split.pane_a ||
      TabStripBridge::KeyForTab(tab_b) != split.pane_b) {
    return CommandErr(CommandError::kStaleTabReference);
  }

  // M149 AddToNewSplit merges the active tab with one explicit sorted index.
  // Seoul validates both panes, explicitly activates pane B, re-resolves, then
  // dispatches AddToNewSplit with pane A's current index.
  strip->ActivateTabAt(index_b);
  index_a = resolve_index(split.pane_a);
  index_b = resolve_index(split.pane_b);
  if (index_a < 0 || index_b < 0 || !strip->ContainsIndex(index_a) ||
      !strip->ContainsIndex(index_b)) {
    return CommandErr(CommandError::kStaleTabReference);
  }
  if (strip->active_index() != index_b) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  if (index_a == index_b) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }

  const std::vector<int> indices = {index_a};
  split_tabs::SplitTabVisualData visual(split_tabs::SplitTabLayout::kHorizontal,
                                        ratio);
  const split_tabs::SplitTabId split_id = strip->AddToNewSplit(
      indices, visual, split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
  if (upstream_token) {
    *upstream_token = split_id.ToString();
  }
  return CommandOk();
}

CommandStatusResult ChromiumMutationAdapterImpl::DissolveSplit(
    Profile* profile,
    LiveWindowKey window,
    const std::string& upstream_token) {
  BrowserWindowInterface* browser = FindBrowser(profile, window);
  TabStripModel* strip = browser ? browser->GetTabStripModel() : nullptr;
  if (!strip || upstream_token.empty()) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  // SplitTabId is a TokenId with no default constructor, so track the match in
  // an optional rather than default-constructing an empty id.
  std::optional<split_tabs::SplitTabId> split_id;
  for (const split_tabs::SplitTabId& candidate : strip->ListSplits()) {
    if (candidate.ToString() == upstream_token) {
      split_id = candidate;
      break;
    }
  }
  if (!split_id || split_id->is_empty() || !strip->ContainsSplit(*split_id)) {
    return CommandErr(CommandError::kSplitPreconditionFailure);
  }
  strip->RemoveSplit(*split_id);
  return CommandOk();
}

}  // namespace seoul
