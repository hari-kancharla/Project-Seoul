// Project Seoul session-restore metadata bridge.

#include "seoul/browser/lifecycle/session_restore_metadata.h"

#include <utility>

#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "seoul/browser/lifecycle/new_tab_placeholder_provenance.h"
#include "seoul/browser/organization/organization_limits.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace seoul {

namespace {

class SeoulSessionMetadata
    : public content::WebContentsUserData<SeoulSessionMetadata> {
 public:
  SeoulSessionMetadata(const SeoulSessionMetadata&) = delete;
  SeoulSessionMetadata& operator=(const SeoulSessionMetadata&) = delete;
  ~SeoulSessionMetadata() override = default;

  const TabMembershipId& membership() const { return membership_; }
  void set_membership(TabMembershipId membership) {
    membership_ = std::move(membership);
  }

 private:
  explicit SeoulSessionMetadata(content::WebContents* contents)
      : content::WebContentsUserData<SeoulSessionMetadata>(*contents) {}

  friend class content::WebContentsUserData<SeoulSessionMetadata>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  TabMembershipId membership_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(SeoulSessionMetadata);

void SetMembership(content::WebContents* contents,
                   const TabMembershipId& membership) {
  if (!contents || !membership.is_valid()) {
    return;
  }
  SeoulSessionMetadata::CreateForWebContents(contents);
  if (SeoulSessionMetadata* metadata =
          SeoulSessionMetadata::FromWebContents(contents)) {
    metadata->set_membership(membership);
  }
}

}  // namespace

void PopulateSeoulSessionMetadata(
    const content::WebContents* contents,
    std::map<std::string, std::string>* extra_data) {
  if (!contents || !extra_data) {
    return;
  }
  const TabMembershipId membership = RestoredMembershipForTab(contents);
  if (membership.is_valid()) {
    (*extra_data)[kSeoulMembershipSessionKey] = membership.value();
  }
  if (HasSyntheticNewTabPlaceholderProvenance(contents) &&
      contents->GetLastCommittedURL() == GURL(url::kAboutBlankURL)) {
    (*extra_data)[kSeoulSyntheticNewTabPlaceholderSessionKey] = "1";
  }
}

void RestoreSeoulSessionMetadata(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  if (!contents) {
    return;
  }

  const auto membership_found = extra_data.find(kSeoulMembershipSessionKey);
  if (membership_found != extra_data.end() &&
      !membership_found->second.empty() &&
      membership_found->second.size() <= kMaxTabKeyLength) {
    const TabMembershipId membership =
        TabMembershipId::FromString(membership_found->second);
    if (membership.is_valid()) {
      SetMembership(contents, membership);
    }
  }

  const auto placeholder_found =
      extra_data.find(kSeoulSyntheticNewTabPlaceholderSessionKey);
  if (placeholder_found != extra_data.end() &&
      placeholder_found->second == "1") {
    MarkSyntheticNewTabPlaceholder(contents);
  }
}

TabMembershipId RestoredMembershipForTab(const content::WebContents* contents) {
  const SeoulSessionMetadata* metadata =
      contents ? SeoulSessionMetadata::FromWebContents(contents) : nullptr;
  return metadata ? metadata->membership() : TabMembershipId();
}

bool PersistSeoulSessionMetadata(BrowserWindowInterface* browser,
                                 content::WebContents* contents,
                                 const TabMembershipId& membership) {
  if (!browser || !contents || !membership.is_valid() ||
      browser->IsDeleteScheduled()) {
    return false;
  }
  const SessionID& window = browser->GetSessionID();
  const SessionID tab = sessions::SessionTabHelper::IdForTab(contents);
  if (!window.is_valid() || !tab.is_valid()) {
    return false;
  }
  // Keep the durable identity on the live WebContents even if SessionService
  // has not been created yet. The lifecycle bridge retries the command-log
  // write after Chromium finishes dispatching the insertion.
  SetMembership(contents, membership);
  SessionService* session_service =
      SessionServiceFactory::GetForProfile(browser->GetProfile());
  if (!session_service) {
    return false;
  }
  session_service->AddTabExtraData(window, tab, kSeoulMembershipSessionKey,
                                   membership.value());
  return true;
}

bool PersistSeoulSyntheticNewTabPlaceholderMetadata(
    BrowserWindowInterface* browser,
    content::WebContents* contents) {
  if (!browser || !contents || browser->IsDeleteScheduled() ||
      !HasSyntheticNewTabPlaceholderProvenance(contents)) {
    return false;
  }
  const SessionID& window = browser->GetSessionID();
  const SessionID tab = sessions::SessionTabHelper::IdForTab(contents);
  if (!window.is_valid() || !tab.is_valid()) {
    return false;
  }
  SessionService* session_service =
      SessionServiceFactory::GetForProfile(browser->GetProfile());
  if (!session_service) {
    return false;
  }
  const bool is_inert_placeholder =
      contents->GetVisibleURL() == GURL(url::kAboutBlankURL);
  session_service->AddTabExtraData(window, tab,
                                   kSeoulSyntheticNewTabPlaceholderSessionKey,
                                   is_inert_placeholder ? "1" : std::string());
  return true;
}

}  // namespace seoul
