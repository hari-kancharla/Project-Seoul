// Project Seoul native lifecycle bridge.
// Ephemeral live-window snapshot types (Chromium-free).

#ifndef SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_SNAPSHOT_TYPES_H_
#define SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_SNAPSHOT_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "seoul/browser/lifecycle/lifecycle_identity.h"

namespace seoul {

struct LiveTabDescriptor {
  LiveTabDescriptor();
  LiveTabDescriptor(const LiveTabDescriptor&);
  LiveTabDescriptor(LiveTabDescriptor&&);
  LiveTabDescriptor& operator=(const LiveTabDescriptor&);
  LiveTabDescriptor& operator=(LiveTabDescriptor&&);
  ~LiveTabDescriptor();
  LiveTabKey tab;
  // Ephemeral, data-minimized presentation metadata. The title is bounded by
  // the provider and the origin never includes a path, query, or fragment.
  std::string title;
  std::string origin;
  int strip_order = -1;
  bool chromium_pinned = false;
  std::string upstream_split_token;
  std::string upstream_group_token;
  bool is_active = false;
  // True only for the one inert startup tab adopted as this window's
  // reusable new-tab placeholder. This is ephemeral lifecycle state, not a
  // URL-derived label for every New Tab Page.
  bool is_new_tab_placeholder = false;

  friend bool operator==(const LiveTabDescriptor&,
                         const LiveTabDescriptor&) = default;
};

struct LiveWindowSnapshot {
  LiveWindowSnapshot();
  LiveWindowSnapshot(const LiveWindowSnapshot&);
  LiveWindowSnapshot(LiveWindowSnapshot&&);
  LiveWindowSnapshot& operator=(const LiveWindowSnapshot&);
  LiveWindowSnapshot& operator=(LiveWindowSnapshot&&);
  ~LiveWindowSnapshot();
  LiveWindowKey window;
  bool eligible = true;
  bool lifecycle_degraded = false;
  std::vector<LiveTabDescriptor> tabs;
  LiveTabKey active_tab;
  uint64_t revision = 0;
};

// Alias used by projection calculator.
using LiveWindowTabState = LiveWindowSnapshot;

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_LIVE_WINDOW_SNAPSHOT_TYPES_H_
