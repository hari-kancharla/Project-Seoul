// Project Seoul Handset - User-Agent and client hints.
//
// Sites decide which layout to serve from two independent signals: the legacy
// User-Agent string, and the Sec-CH-UA-* client hints exposed to script as
// navigator.userAgentData. Overriding one and leaving the other describing a
// desktop Mac is the single most common way device emulation fails - the site
// reads the hint, disbelieves the string, and serves the desktop page.
//
// So this module answers both together, and answers them the way the emulated
// device actually would: an Android profile presents Chrome's mobile string
// with a matching, complete hint set; an iOS profile presents Safari's string
// and requests no hints, because Safari implements no client hints and a real
// iPhone therefore sends none. Emitting hints for an iOS profile would be a
// combination no device produces, and sites that check for it can tell.
//
// Known, deliberate gap on the iOS path. Suppressing the metadata suppresses
// the Sec-CH-UA-* request headers, but it does not remove
// navigator.userAgentData: blink materializes an absent metadata override into
// a default-constructed one before it reaches script, so the object still
// exists and reports mobile:false with an empty platform and brand list. A
// real iPhone leaves the property undefined. A site that branches on the
// headers, or on the User-Agent string, sees a phone; a site that branches on
// navigator.userAgentData.mobile does not. Closing that needs the property
// hidden in the renderer, which is not something this module can decide, so it
// is recorded here rather than quietly assumed away.

#ifndef SEOUL_BROWSER_HANDSET_USER_AGENT_PROFILE_H_
#define SEOUL_BROWSER_HANDSET_USER_AGENT_PROFILE_H_

#include <string>
#include <string_view>
#include <vector>

#include "seoul/browser/handset/handset_types.h"

namespace seoul {

// Everything the integration layer needs to populate a
// blink::UserAgentOverride, with no rules left for it to apply.
struct HandsetUserAgent {
  HandsetUserAgent();
  HandsetUserAgent(const HandsetUserAgent&);
  HandsetUserAgent& operator=(const HandsetUserAgent&);
  ~HandsetUserAgent();

  std::string ua_string;

  // When false the override carries the string alone and no client-hint
  // request headers are sent. The integration layer must leave
  // ua_metadata_override unset in that case. See the header comment for the
  // one place this still differs from a real device: the script-visible
  // navigator.userAgentData object survives the suppression.
  bool send_client_hints = false;

  // Valid only when send_client_hints is true.
  std::string platform;
  std::string platform_version;
  std::string model;
  std::string architecture;
  bool mobile = false;
  // Sec-CH-UA-Form-Factors, restricted to the values blink accepts.
  std::vector<std::string> form_factors;
};

// Builds the pair for `profile`. `browser_major_version` is the running
// Chromium major version, threaded in rather than read from a global so the
// rules stay pure and the test can pin a version.
HandsetUserAgent BuildHandsetUserAgent(const HandsetProfile& profile,
                                       std::string_view browser_major_version);

// Converts an iOS platform version from User-Agent form to client-hint form
// ("18_0" to "18.0"). Exposed because the two forms appear in the same string
// and mixing them is a silent, hard-to-see defect.
std::string HandsetDottedVersion(std::string_view underscored_version);

}  // namespace seoul

#endif  // SEOUL_BROWSER_HANDSET_USER_AGENT_PROFILE_H_
