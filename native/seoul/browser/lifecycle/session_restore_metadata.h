// Project Seoul session-restore metadata bridge.
// Persists bounded Seoul-owned tab metadata in Chromium's existing session
// extra-data channel: the opaque organization membership id and one explicit
// bit that distinguishes Seoul's inert startup placeholder from a user's
// ordinary about:blank tab.

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
inline constexpr char kSeoulSyntheticNewTabPlaceholderSessionKey[] =
    "seoul.synthetic_new_tab_placeholder";

// Adds the live tab's bounded Seoul metadata to Chromium's session snapshot.
// Called from BrowserLiveTabContext so command-log rebuilds cannot discard
// metadata previously written through SessionService.
void PopulateSeoulSessionMetadata(
    const content::WebContents* contents,
    std::map<std::string, std::string>* extra_data);

// Called by Chromium's restored-tab construction seam before the WebContents
// enters a TabStripModel. Invalid, missing, or oversized ids are ignored.
void RestoreSeoulSessionMetadata(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data);

// Returns the validated durable membership carried by a restored tab.
TabMembershipId RestoredMembershipForTab(const content::WebContents* contents);

// Writes the current durable membership into Chromium's session command log
// and mirrors it on the live WebContents. Returns false when the browser/tab
// identity or SessionService is unavailable; organization state remains the
// authority and the next lifecycle publication may retry.
bool PersistSeoulSessionMetadata(BrowserWindowInterface* browser,
                                 content::WebContents* contents,
                                 const TabMembershipId& membership);

// Persists the explicit synthetic-placeholder provenance bit. Once that
// WebContents leaves its inert about:blank state, the stored bit is cleared.
// Ordinary user/restored about:blank tabs never receive this metadata.
bool PersistSeoulSyntheticNewTabPlaceholderMetadata(
    BrowserWindowInterface* browser,
    content::WebContents* contents);

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_SESSION_RESTORE_METADATA_H_
