// Project Seoul native lifecycle bridge.

#include "seoul/browser/lifecycle/window_watcher.h"

#include <utility>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/core/session_id.h"
#include "seoul/browser/lifecycle/lifecycle_coordinator.h"
#include "seoul/browser/lifecycle/lifecycle_events.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"

namespace seoul {

WindowWatcher::WindowWatcher(Profile *profile,
                             LifecycleCoordinator *coordinator,
                             OrganizationModel *organization)
    : profile_(profile), coordinator_(coordinator), organization_(organization),
      live_state_provider_(std::make_unique<LiveWindowStateProvider>()) {}

WindowWatcher::~WindowWatcher() = default;

void WindowWatcher::StartObserving() {
  ProfileBrowserCollection *collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  if (!collection) {
    return;
  }
  if (!observation_.IsObserving()) {
    observation_.Observe(collection);
  }
  collection->ForEach([this](BrowserWindowInterface *browser) {
    if (IsEligible(browser)) {
      Track(browser);
    }
    return true;
  });
}

void WindowWatcher::RescanExistingWindows() {
  for (auto &[key, bridge] : bridges_) {
    if (bridge) {
      bridge->RescanExistingState();
    }
  }
}

// static
bool WindowWatcher::IsEligible(BrowserWindowInterface *browser) {
  return browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
         !browser->IsDeleteScheduled();
}

void WindowWatcher::OnBrowserCreated(BrowserWindowInterface *browser) {
  if (IsEligible(browser)) {
    Track(browser);
  }
}

void WindowWatcher::OnBrowserClosed(BrowserWindowInterface *browser) {
  Untrack(browser);
}

void WindowWatcher::Track(BrowserWindowInterface *browser) {
  const SessionID &sid = browser->GetSessionID();
  if (!sid.is_valid()) {
    return;
  }
  const LiveWindowKey key = LiveWindowKey::FromSessionId(sid.id());
  if (bridges_.count(key)) {
    return;
  }
  TabStripModel *model = browser->GetTabStripModel();
  if (!model) {
    return;
  }
  auto bridge = std::make_unique<TabStripBridge>(
      key, browser, model, coordinator_, live_state_provider_.get(),
      organization_);

  NormalizedEvent event;
  event.type = NormalizedEventType::kWindowDiscovered;
  event.window = key;
  event.persisted_window = PersistedWindowRef::FromSessionId(sid.id());
  coordinator_->OnNormalizedEvent(event);

  bridge->EnumerateExistingState();
  bridges_[key] = std::move(bridge);
}

void WindowWatcher::Untrack(BrowserWindowInterface *browser) {
  const SessionID &sid = browser->GetSessionID();
  if (!sid.is_valid()) {
    return;
  }
  const LiveWindowKey key = LiveWindowKey::FromSessionId(sid.id());
  auto it = bridges_.find(key);
  if (it == bridges_.end()) {
    return;
  }
  NormalizedEvent event;
  event.type = NormalizedEventType::kWindowDestroyed;
  event.window = key;
  coordinator_->OnNormalizedEvent(event);
  live_state_provider_->RemoveWindow(key);
  bridges_.erase(it);
}

} // namespace seoul
