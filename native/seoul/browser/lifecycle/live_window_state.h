// Project Seoul native lifecycle bridge.

#ifndef SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_STATE_H_
#define SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_STATE_H_

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "seoul/browser/lifecycle/live_window_snapshot_types.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace seoul {

// Privacy-safe inputs used to decide whether a tab can be treated as the one
// inert startup placeholder. Keeping this policy independent from
// WebContents makes the safety boundary directly unit-testable.
struct NewTabPlaceholderNavigationState {
  bool has_synthetic_startup_provenance = false;
  bool has_opener = false;
  bool is_initial_blank_navigation = false;
  int entry_count = 0;
  bool visible_or_committed_placeholder_page = false;
  bool has_pending_entry = false;
  bool pending_entry_is_placeholder_page = false;
};

bool IsSafeNewTabPlaceholderCandidate(
    const NewTabPlaceholderNavigationState& state);
bool IsSafeNewTabPlaceholderCandidate(content::WebContents* contents);

class LiveWindowStateObserver : public base::CheckedObserver {
 public:
  ~LiveWindowStateObserver() override = default;
  virtual void OnLiveWindowSnapshotChanged(const LiveWindowSnapshot& snapshot) {
  }
  virtual void OnLiveWindowRemoved(LiveWindowKey window) {}
};

class LiveWindowStateProvider {
 public:
  LiveWindowStateProvider();
  LiveWindowStateProvider(const LiveWindowStateProvider&) = delete;
  LiveWindowStateProvider& operator=(const LiveWindowStateProvider&) = delete;
  ~LiveWindowStateProvider();

  void SetLifecycleDegraded(bool degraded);

  std::optional<LiveWindowSnapshot> GetSnapshot(LiveWindowKey window) const;
  // Keys of every window with a published snapshot, in deterministic order.
  std::vector<LiveWindowKey> Windows() const;
  // Returns true only for the single startup placeholder adopted for
  // `window`. A later user-created or user-navigated NTP is never inferred to
  // be a placeholder from its URL alone.
  bool IsNewTabPlaceholder(LiveWindowKey window, LiveTabKey tab) const;
  void PublishSnapshot(LiveWindowKey window, TabStripModel* model);
  void RemoveWindow(LiveWindowKey window);

  void AddObserver(LiveWindowStateObserver* observer);
  void RemoveObserver(LiveWindowStateObserver* observer);

  void SetSnapshotForTesting(LiveWindowKey window, LiveWindowSnapshot snapshot);

 private:
  LiveWindowSnapshot BuildSnapshot(LiveWindowKey window, TabStripModel* model);
  void NotifySnapshot(const LiveWindowSnapshot& snapshot);

  bool lifecycle_degraded_ = false;
  std::map<LiveWindowKey, LiveWindowSnapshot> snapshots_;
  std::map<LiveWindowKey, LiveTabKey> new_tab_placeholders_;
  // Adoption becomes one-shot only after an explicitly marked synthetic
  // placeholder is found. A restored/user NTP must not consume this guard.
  std::set<LiveWindowKey> placeholder_adoption_attempted_;
  uint64_t next_revision_ = 1;
  base::ObserverList<LiveWindowStateObserver> observers_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_STATE_H_
