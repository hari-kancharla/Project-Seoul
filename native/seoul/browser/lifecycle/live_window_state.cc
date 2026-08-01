// Project Seoul native lifecycle bridge.

#include "seoul/browser/lifecycle/live_window_state.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "seoul/browser/lifecycle/new_tab_placeholder_provenance.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

constexpr size_t kMaxLiveTabTitleLength = 256;

bool IsPlaceholderPageUrl(const GURL& url) {
  return url == GURL(url::kAboutBlankURL) ||
         (url.SchemeIs(content::kChromeUIScheme) &&
          (url.host() == chrome::kChromeUINewTabHost ||
           url.host() == chrome::kChromeUINewTabPageHost));
}

}  // namespace

bool IsSafeNewTabPlaceholderCandidate(
    const NewTabPlaceholderNavigationState& state) {
  if (!state.has_synthetic_startup_provenance || state.has_opener ||
      state.entry_count > 1) {
    return false;
  }

  // Never let a stale NTP/initial-blank URL mask a real pending navigation.
  // This is the hard safety boundary for BrowserView's Stop() call.
  if (state.has_pending_entry && !state.pending_entry_is_placeholder_page) {
    return false;
  }

  return state.is_initial_blank_navigation ||
         state.visible_or_committed_placeholder_page ||
         state.pending_entry_is_placeholder_page;
}

bool IsSafeNewTabPlaceholderCandidate(content::WebContents* contents) {
  if (!contents) {
    return false;
  }

  content::NavigationController& controller = contents->GetController();
  content::NavigationEntry* pending = controller.GetPendingEntry();
  return IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance =
           HasSyntheticNewTabPlaceholderProvenance(contents),
       .has_opener = contents->HasOpener(),
       .is_initial_blank_navigation = controller.IsInitialBlankNavigation(),
       .entry_count = controller.GetEntryCount(),
       .visible_or_committed_placeholder_page =
           IsPlaceholderPageUrl(contents->GetVisibleURL()) ||
           IsPlaceholderPageUrl(contents->GetLastCommittedURL()),
       .has_pending_entry = pending != nullptr,
       .pending_entry_is_placeholder_page =
           pending && (IsPlaceholderPageUrl(pending->GetURL()) ||
                       IsPlaceholderPageUrl(pending->GetVirtualURL()))});
}

LiveWindowStateProvider::LiveWindowStateProvider() = default;
LiveWindowStateProvider::~LiveWindowStateProvider() = default;

void LiveWindowStateProvider::SetLifecycleDegraded(bool degraded) {
  lifecycle_degraded_ = degraded;
}

std::optional<LiveWindowSnapshot> LiveWindowStateProvider::GetSnapshot(
    LiveWindowKey window) const {
  auto it = snapshots_.find(window);
  if (it == snapshots_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<LiveWindowKey> LiveWindowStateProvider::Windows() const {
  std::vector<LiveWindowKey> keys;
  keys.reserve(snapshots_.size());
  for (const auto& [key, snapshot] : snapshots_) {
    keys.push_back(key);
  }
  return keys;
}

bool LiveWindowStateProvider::IsNewTabPlaceholder(LiveWindowKey window,
                                                  LiveTabKey tab) const {
  if (!tab.is_valid()) {
    return false;
  }
  const auto it = new_tab_placeholders_.find(window);
  return it != new_tab_placeholders_.end() && it->second == tab;
}

LiveWindowSnapshot LiveWindowStateProvider::BuildSnapshot(
    LiveWindowKey window,
    TabStripModel* model) {
  LiveWindowSnapshot snapshot;
  snapshot.window = window;
  snapshot.eligible = true;
  snapshot.lifecycle_degraded = lifecycle_degraded_;
  if (!model) {
    return snapshot;
  }
  const int count = model->count();
  std::vector<bool> placeholder_candidates;
  placeholder_candidates.reserve(count);
  for (int index = 0; index < count; ++index) {
    tabs::TabInterface* tab = model->GetTabAtIndex(index);
    if (!tab) {
      continue;
    }
    LiveTabDescriptor descriptor;
    descriptor.tab = TabStripBridge::KeyForTab(tab);
    if (content::WebContents* contents = tab->GetContents()) {
      std::u16string title = contents->GetTitle();
      if (title.size() > kMaxLiveTabTitleLength) {
        title.resize(kMaxLiveTabTitleLength);
      }
      descriptor.title = base::UTF16ToUTF8(title);
      const url::Origin origin =
          url::Origin::Create(contents->GetLastCommittedURL());
      if (!origin.opaque() && origin.GetURL().SchemeIsHTTPOrHTTPS()) {
        descriptor.origin = origin.Serialize();
      }
    }
    descriptor.strip_order = index;
    descriptor.chromium_pinned = model->IsTabPinned(index);
    descriptor.is_active = model->active_index() == index;
    const std::optional<split_tabs::SplitTabId> split_id =
        model->GetSplitForTab(index);
    if (split_id.has_value()) {
      descriptor.upstream_split_token = split_id->ToString();
    }
    const std::optional<tab_groups::TabGroupId> group_id =
        model->GetTabGroupForTab(index);
    if (group_id.has_value()) {
      descriptor.upstream_group_token = group_id->ToString();
    }
    placeholder_candidates.push_back(
        !descriptor.chromium_pinned &&
        descriptor.upstream_split_token.empty() &&
        descriptor.upstream_group_token.empty() &&
        IsSafeNewTabPlaceholderCandidate(tab->GetContents()));
    snapshot.tabs.push_back(descriptor);
  }

  auto placeholder = new_tab_placeholders_.find(window);
  if (placeholder != new_tab_placeholders_.end()) {
    bool still_valid = false;
    for (size_t index = 0; index < snapshot.tabs.size(); ++index) {
      if (snapshot.tabs[index].tab == placeholder->second &&
          placeholder_candidates[index]) {
        still_valid = true;
        break;
      }
    }
    if (!still_valid) {
      new_tab_placeholders_.erase(placeholder);
      placeholder = new_tab_placeholders_.end();
    }
  }

  // Adopt at most one pre-existing active blank/NTP tab, exactly once for the
  // life of the window. New tabs inserted after this initial snapshot remain
  // ordinary user-visible tabs even if they also display an NTP.
  if (placeholder == new_tab_placeholders_.end() &&
      !placeholder_adoption_attempted_.contains(window)) {
    for (size_t index = 0; index < snapshot.tabs.size(); ++index) {
      if (snapshot.tabs[index].is_active && placeholder_candidates[index] &&
          snapshot.tabs[index].tab.is_valid()) {
        placeholder_adoption_attempted_.insert(window);
        placeholder =
            new_tab_placeholders_.emplace(window, snapshot.tabs[index].tab)
                .first;
        break;
      }
    }
  }

  if (placeholder != new_tab_placeholders_.end()) {
    for (LiveTabDescriptor& descriptor : snapshot.tabs) {
      descriptor.is_new_tab_placeholder = descriptor.tab == placeholder->second;
    }
  }

  const int active = model->active_index();
  if (active >= 0 && active < count) {
    snapshot.active_tab =
        TabStripBridge::KeyForTab(model->GetTabAtIndex(active));
  }
  return snapshot;
}

void LiveWindowStateProvider::PublishSnapshot(LiveWindowKey window,
                                              TabStripModel* model) {
  LiveWindowSnapshot snapshot = BuildSnapshot(window, model);
  auto it = snapshots_.find(window);
  if (it != snapshots_.end() && it->second.tabs == snapshot.tabs &&
      it->second.active_tab == snapshot.active_tab &&
      it->second.lifecycle_degraded == snapshot.lifecycle_degraded) {
    return;
  }
  snapshot.revision = next_revision_++;
  snapshots_[window] = snapshot;
  NotifySnapshot(snapshot);
}

void LiveWindowStateProvider::RemoveWindow(LiveWindowKey window) {
  snapshots_.erase(window);
  new_tab_placeholders_.erase(window);
  placeholder_adoption_attempted_.erase(window);
  for (LiveWindowStateObserver& observer : observers_) {
    observer.OnLiveWindowRemoved(window);
  }
}

void LiveWindowStateProvider::AddObserver(LiveWindowStateObserver* observer) {
  observers_.AddObserver(observer);
}

void LiveWindowStateProvider::RemoveObserver(
    LiveWindowStateObserver* observer) {
  observers_.RemoveObserver(observer);
}

void LiveWindowStateProvider::SetSnapshotForTesting(
    LiveWindowKey window,
    LiveWindowSnapshot snapshot) {
  auto it = snapshots_.find(window);
  if (it != snapshots_.end() && it->second.tabs == snapshot.tabs &&
      it->second.active_tab == snapshot.active_tab &&
      it->second.lifecycle_degraded == snapshot.lifecycle_degraded) {
    return;
  }
  snapshot.revision = next_revision_++;
  snapshots_[window] = snapshot;
  NotifySnapshot(snapshot);
}

void LiveWindowStateProvider::NotifySnapshot(
    const LiveWindowSnapshot& snapshot) {
  for (LiveWindowStateObserver& observer : observers_) {
    observer.OnLiveWindowSnapshotChanged(snapshot);
  }
}

}  // namespace seoul
