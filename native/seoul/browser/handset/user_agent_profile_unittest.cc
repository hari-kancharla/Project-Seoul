// Project Seoul Handset.
// Unit tests for the User-Agent string and client-hint pair.

#include "seoul/browser/handset/user_agent_profile.h"

#include <algorithm>

#include "base/check.h"
#include "base/strings/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

constexpr char kMajorVersion[] = "149";

const HandsetProfile& Profile(const std::string& id) {
  const HandsetProfile* profile = FindHandsetProfile(id);
  CHECK(profile) << id;
  return *profile;
}

HandsetUserAgent Build(const std::string& id) {
  return BuildHandsetUserAgent(Profile(id), kMajorVersion);
}

TEST(HandsetUserAgentTest, DottedVersionConvertsEveryUnderscore) {
  EXPECT_EQ("18.0", HandsetDottedVersion("18_0"));
  EXPECT_EQ("17.6.1", HandsetDottedVersion("17_6_1"));
  EXPECT_EQ("15", HandsetDottedVersion("15"));
  EXPECT_EQ("", HandsetDottedVersion(""));
}

TEST(HandsetUserAgentTest, IOSPhonePresentsSafari) {
  const HandsetUserAgent agent = Build("iphone");
  EXPECT_NE(std::string::npos, agent.ua_string.find("(iPhone; CPU iPhone OS "
                                                    "18_0 like Mac OS X)"));
  EXPECT_NE(std::string::npos, agent.ua_string.find("Version/18.0"));
  EXPECT_NE(std::string::npos, agent.ua_string.find("Mobile/15E148"));
  EXPECT_NE(std::string::npos, agent.ua_string.find("Safari/604.1"));
  // A Safari string must not also claim to be Chrome.
  EXPECT_EQ(std::string::npos, agent.ua_string.find("Chrome/"));
}

TEST(HandsetUserAgentTest, IOSSendsNoClientHints) {
  // The regression this pins: emitting hints alongside a Safari string is a
  // combination no real device produces.
  for (const char* id : {"iphone", "iphone-compact", "iphone-max", "tablet",
                         "tablet-compact"}) {
    const HandsetUserAgent agent = Build(id);
    EXPECT_FALSE(agent.send_client_hints) << id;
    EXPECT_TRUE(agent.platform.empty()) << id;
    EXPECT_TRUE(agent.form_factors.empty()) << id;
    EXPECT_FALSE(agent.mobile) << id;
  }
}

TEST(HandsetUserAgentTest, IPadIdentifiesAsIPadAndStaysMobile) {
  const HandsetUserAgent agent = Build("tablet");
  EXPECT_NE(std::string::npos,
            agent.ua_string.find("(iPad; CPU OS 18_0 like Mac OS X)"));
  EXPECT_EQ(std::string::npos, agent.ua_string.find("iPhone"));
  // iPadOS keeps the Mobile/ build token; sites branch on it.
  EXPECT_NE(std::string::npos, agent.ua_string.find("Mobile/15E148"));
}

TEST(HandsetUserAgentTest, AndroidPhonePresentsChromeWithMatchingHints) {
  const HandsetUserAgent agent = Build("android");
  EXPECT_NE(std::string::npos,
            agent.ua_string.find("(Linux; Android 15; Pixel 8)"));
  EXPECT_NE(std::string::npos, agent.ua_string.find("Chrome/149.0.0.0"));
  EXPECT_NE(std::string::npos, agent.ua_string.find(" Mobile Safari/537.36"));

  EXPECT_TRUE(agent.send_client_hints);
  EXPECT_EQ("Android", agent.platform);
  EXPECT_EQ("15", agent.platform_version);
  EXPECT_EQ("Pixel 8", agent.model);
  EXPECT_EQ("arm", agent.architecture);
  EXPECT_TRUE(agent.mobile);
  ASSERT_EQ(1u, agent.form_factors.size());
  EXPECT_EQ("Mobile", agent.form_factors.front());
}

TEST(HandsetUserAgentTest, StringAndHintsAgreeOnPlatformVersionAndModel) {
  // The two signals are read independently; a site that compares them must not
  // find them disagreeing.
  const HandsetUserAgent agent = Build("android-compact");
  const HandsetProfile& profile = Profile("android-compact");
  EXPECT_EQ(profile.platform_version, agent.platform_version);
  EXPECT_EQ(profile.model, agent.model);
  EXPECT_NE(std::string::npos,
            agent.ua_string.find("Android " + profile.platform_version));
  EXPECT_NE(std::string::npos, agent.ua_string.find(profile.model));
}

TEST(HandsetUserAgentTest, MajorVersionIsThreadedThroughNotHardcoded) {
  const HandsetUserAgent agent =
      BuildHandsetUserAgent(Profile("android"), "151");
  EXPECT_NE(std::string::npos, agent.ua_string.find("Chrome/151.0.0.0"));
  EXPECT_EQ(std::string::npos, agent.ua_string.find("Chrome/149"));
}

TEST(HandsetUserAgentTest, EveryProfileProducesAWellFormedString) {
  for (const HandsetProfile& profile : HandsetProfiles()) {
    const HandsetUserAgent agent =
        BuildHandsetUserAgent(profile, kMajorVersion);
    EXPECT_TRUE(base::StartsWith(agent.ua_string, "Mozilla/5.0 ("))
        << profile.id;
    // An unbalanced parenthesis means a profile field leaked into the comment
    // clause; some servers reject the request outright.
    EXPECT_EQ(std::count(agent.ua_string.begin(), agent.ua_string.end(), '('),
              std::count(agent.ua_string.begin(), agent.ua_string.end(), ')'))
        << profile.id;
    EXPECT_EQ(std::string::npos, agent.ua_string.find("  ")) << profile.id;
    EXPECT_EQ(std::string::npos, agent.ua_string.find("Macintosh"))
        << profile.id;
  }
}

}  // namespace
}  // namespace seoul
