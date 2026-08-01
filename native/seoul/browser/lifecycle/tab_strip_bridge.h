// Project Seoul native lifecycle bridge.
// One thin adapter per eligible browser window. It observes ONLY that window's
// TabStripModel and translates verified Chromium callbacks into
// NormalizedEvents. It holds no organization logic and never mutates the
// browser. It stops observing before the tab strip becomes invalid.

#ifndef SEOUL_BROWSER_LIFECYCLE_TAB_STRIP_BRIDGE_H_
#define SEOUL_BROWSER_LIFECYCLE_TAB_STRIP_BRIDGE_H_

#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "seoul/browser/lifecycle/lifecycle_events.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"

class BrowserWindowInterface;
class TabStripModel;
namespace content {
class WebContents;
}
namespace tabs {
class TabInterface;
}

namespace seoul {

class LifecycleEventSink;
class LiveWindowStateProvider;
class OrganizationModel;

class TabStripBridge : public TabStripModelObserver {
 public:
  TabStripBridge(LiveWindowKey window,
                 BrowserWindowInterface* browser,
                 TabStripModel* model,
                 LifecycleEventSink* sink,
                 LiveWindowStateProvider* live_state,
                 OrganizationModel* organization);
  TabStripBridge(const TabStripBridge&) = delete;
  TabStripBridge& operator=(const TabStripBridge&) = delete;
  ~TabStripBridge() override;

  LiveWindowKey window() const { return window_; }

  // Pure, stateless conversions from a tab (or its contents) to a LiveTabKey.
  // Public because LiveWindowStateProvider derives the same keys when it takes
  // its own bounded snapshots.
  static LiveTabKey KeyForContents(const content::WebContents* contents);
  static LiveTabKey KeyForTab(const tabs::TabInterface* tab);

  // Bounded snapshot of tabs and splits already present when the bridge
  // attaches. Idempotent on first call; RescanExistingState() re-inspects.
  void EnumerateExistingState();

  // Re-inspect the current tab strip and emit reconciliation events for tabs,
  // active state, pinned state, and splits that changed since the last scan.
  void RescanExistingState();

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabCloseCancelled(const tabs::TabInterface* tab) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

 private:
  void EmitTabEvent(NormalizedEventType type,
                    const LiveTabKey& tab,
                    int order_index,
                    int batch_sequence,
                    TabInsertKind insert_kind = TabInsertKind::kUnknown,
                    TabRemovalKind removal_kind = TabRemovalKind::kUnknown);
  TabRemovalKind ClassifyRemoval(TabRemovedReason reason) const;
  content::WebContents* ContentsForKey(const LiveTabKey& tab) const;
  void PersistMembershipForTab(const LiveTabKey& tab);
  void ScheduleMembershipPersistence(const LiveTabKey& tab);
  void PersistSyntheticPlaceholderForTab(const LiveTabKey& tab);
  void ScheduleSyntheticPlaceholderPersistence(const LiveTabKey& tab);

  void PublishLiveSnapshot();

  const LiveWindowKey window_;
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripModel> model_;
  raw_ptr<LifecycleEventSink> sink_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  raw_ptr<OrganizationModel> organization_;
  bool enumerated_ = false;
  std::set<LiveTabKey> last_enumerated_tabs_;
  std::set<std::string> last_enumerated_splits_;
  base::WeakPtrFactory<TabStripBridge> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_TAB_STRIP_BRIDGE_H_
