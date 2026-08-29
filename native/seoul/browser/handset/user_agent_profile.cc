#include "seoul/browser/handset/user_agent_profile.h"

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace seoul {

HandsetUserAgent::HandsetUserAgent() = default;
HandsetUserAgent::HandsetUserAgent(const HandsetUserAgent&) = default;
HandsetUserAgent& HandsetUserAgent::operator=(const HandsetUserAgent&) =
    default;
HandsetUserAgent::~HandsetUserAgent() = default;

namespace {

// Safari's frozen WebKit tokens. These have not tracked a real version for
// years; every current iOS Safari reports exactly these, so reproducing them
// verbatim is what makes the string match a device rather than approximate it.
constexpr char kSafariWebKitVersion[] = "605.1.15";
constexpr char kSafariBuild[] = "15E148";
constexpr char kSafariSuffix[] = "Safari/604.1";

// Chrome on Android reports the same frozen AppleWebKit token as every other
// Chromium, and a unified minor version of 0.0.0.
constexpr char kChromiumWebKitVersion[] = "537.36";
constexpr char kChromiumFrozenMinorVersion[] = ".0.0.0";

std::string BuildIOSUserAgent(const HandsetProfile& profile) {
  const std::string dotted = HandsetDottedVersion(profile.platform_version);
  // iPadOS names itself iPad and, unlike iPhone, omits the "iPhone" token from
  // the CPU clause. Both carry the Mobile/ build token, which is the token
  // sites actually test for when they branch on iOS.
  const std::string device_clause =
      profile.form_factor == HandsetFormFactor::kTablet
          ? base::StrCat({"iPad; CPU OS ", profile.platform_version,
                          " like Mac OS X"})
          : base::StrCat({"iPhone; CPU iPhone OS ", profile.platform_version,
                          " like Mac OS X"});
  return base::StrCat({"Mozilla/5.0 (", device_clause, ") AppleWebKit/",
                       kSafariWebKitVersion, " (KHTML, like Gecko) Version/",
                       dotted, " Mobile/", kSafariBuild, " ", kSafariSuffix});
}

std::string BuildAndroidUserAgent(const HandsetProfile& profile,
                                  std::string_view browser_major_version) {
  // An Android tablet omits the "Mobile" token. That token, not the screen
  // size, is what Chrome's own request-desktop-site logic and most sites read.
  const char* mobile_token =
      profile.form_factor == HandsetFormFactor::kTablet ? "" : " Mobile";
  return base::StrCat({"Mozilla/5.0 (Linux; Android ", profile.platform_version,
                       "; ", profile.model, ") AppleWebKit/",
                       kChromiumWebKitVersion,
                       " (KHTML, like Gecko) Chrome/", browser_major_version,
                       kChromiumFrozenMinorVersion, mobile_token,
                       " Safari/", kChromiumWebKitVersion});
}

}  // namespace

std::string HandsetDottedVersion(std::string_view underscored_version) {
  std::string dotted(underscored_version);
  base::ReplaceChars(dotted, "_", ".", &dotted);
  return dotted;
}

HandsetUserAgent BuildHandsetUserAgent(
    const HandsetProfile& profile,
    std::string_view browser_major_version) {
  HandsetUserAgent user_agent;

  if (profile.platform == HandsetPlatform::kIOS) {
    user_agent.ua_string = BuildIOSUserAgent(profile);
    // Left false deliberately. See the header: Safari sends no client hints,
    // and a string/hint pair no device produces is itself a signal. The header
    // also records what this does not reach - navigator.userAgentData.
    user_agent.send_client_hints = false;
    return user_agent;
  }

  user_agent.ua_string = BuildAndroidUserAgent(profile, browser_major_version);
  user_agent.send_client_hints = true;
  user_agent.platform = "Android";
  user_agent.platform_version = profile.platform_version;
  user_agent.model = profile.model;
  // Android phones report the 32-bit ARM architecture hint regardless of the
  // 64-bit kernel underneath, matching Chrome on Android.
  user_agent.architecture = "arm";
  user_agent.mobile = profile.form_factor == HandsetFormFactor::kPhone;
  user_agent.form_factors = {
      profile.form_factor == HandsetFormFactor::kTablet ? "Tablet" : "Mobile"};
  return user_agent;
}

}  // namespace seoul
