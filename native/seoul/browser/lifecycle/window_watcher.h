// Project Seoul native lifecycle bridge.
// Per-profile window discovery. Observes the profile's BrowserCollection and
// attaches exactly one TabStripBridge to each eligible normal window, feeding
// window lifecycle into the coordinator. There is no global, all-profile
// observer and no shared cross-window observer: each window gets its own
// bridge.
//
// Confirmed M149 APIs: ProfileBrowserCollection::GetForProfile,
// BrowserCollection
// ::ForEach, BrowserCollectionObserver::OnBrowserCreated/OnBrowserClosed,
// BrowserWindowInterface::GetType/GetSessionID/GetTabStripModel/IsDeleteScheduled,
// observed via base::ScopedObservation<BrowserCollection, ...>.

#ifndef SEOUL_BROWSER_LIFECYCLE_WINDOW_WATCHER_H_
#define SEOUL_BROWSER_LIFECYCLE_WINDOW_WATCHER_H_

#include <cstddef>
#include <map>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"

class Profile;
class BrowserWindowInterface;

namespace seoul {

class LifecycleCoordinator;
class LiveWindowStateProvider;
class OrganizationModel;
class TabStripBridge;

class WindowWatcher : public BrowserCollectionObserver {
public:
  // All arguments must outlive this watcher (the service owns them).
  WindowWatcher(Profile *profile, LifecycleCoordinator *coordinator,
                OrganizationModel *organization);
  WindowWatcher(const WindowWatcher &) = delete;
  WindowWatcher &operator=(const WindowWatcher &) = delete;
  ~WindowWatcher() override;

  // Begin observing and discover windows already open at startup. Idempotent.
  void StartObserving();
  // Rescan tabs and splits for every tracked window (session-restore
  // completion).
  void RescanExistingWindows();
  LiveWindowStateProvider *live_state_provider() {
    return live_state_provider_.get();
  }

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface *browser) override;
  void OnBrowserClosed(BrowserWindowInterface *browser) override;

  size_t tracked_window_count() const { return bridges_.size(); }

private:
  // Eligibility policy: normal tabbed windows only (popup, app, app-popup,
  // devtools, and picture-in-picture windows are not organized in v0).
  static bool IsEligible(BrowserWindowInterface *browser);
  void Track(BrowserWindowInterface *browser);
  void Untrack(BrowserWindowInterface *browser);

  raw_ptr<Profile> profile_;
  raw_ptr<LifecycleCoordinator> coordinator_;
  raw_ptr<OrganizationModel> organization_;
  std::unique_ptr<LiveWindowStateProvider> live_state_provider_;
  std::map<LiveWindowKey, std::unique_ptr<TabStripBridge>> bridges_;
  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_LIFECYCLE_WINDOW_WATCHER_H_
