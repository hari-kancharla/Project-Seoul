// Project Seoul session-restore metadata bridge.
// Persists only an opaque Seoul membership id in Chromium's existing tab
// session extra-data channel. The restored value is attached to WebContents
// until TabStripBridge atomically rebinds the durable organization membership
// to Chromium's new per-session tab id.

#ifndef SEOUL_BROWSER_LIFECYCLE_SESSION_RESTORE_METADATA_H_
#define SEOUL_BROWSER_LIFECYCLE_SESSION_RESTORE_METADATA_H_

#include <map>
#include <string>

#include "seoul/browser/organization/organization_ids.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace seoul {

inline constexpr char kSeoulMembershipSessionKey[] = "seoul.membership_id";

// Adds the live tab's durable Seoul identity to Chromium's bounded session
// snapshot. Called from BrowserLiveTabContext so command-log rebuilds cannot
// discard metadata previously written through SessionService.
void PopulateSeoulSessionMetadata(
    const content::WebContents *contents,
    std::map<std::string, std::string> *extra_data);

// Called by Chromium's restored-tab construction seam before the WebContents
// enters a TabStripModel. Invalid, missing, or oversized ids are ignored.
void RestoreSeoulSessionMetadata(
    content::WebContents *contents,
    const std::map<std::string, std::string> &extra_data);

// Returns the validated durable membership carried by a restored tab.
TabMembershipId RestoredMembershipForTab(const content::WebContents *contents);

// Writes the current durable membership into Chromium's session command log
// and mirrors it on the live WebContents. Returns false when the browser/tab
// identity or SessionService is unavailable; organization state remains the
// authority and the next lifecycle publication may retry.
bool PersistSeoulSessionMetadata(BrowserWindowInterface *browser,
                                 content::WebContents *contents,
                                 const TabMembershipId &membership);

} // namespace seoul

#endif // SEOUL_BROWSER_LIFECYCLE_SESSION_RESTORE_METADATA_H_
