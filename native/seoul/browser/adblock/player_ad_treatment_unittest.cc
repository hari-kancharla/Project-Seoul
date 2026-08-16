// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/adblock/player_ad_treatment.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace seoul::adblock {
namespace {

TEST(PlayerAdTreatmentTest, ServesEveryHttpDocumentAndNothingElse) {
  EXPECT_FALSE(PlayerAdTreatmentScriptFor(GURL("https://example.com/")).empty());
  EXPECT_FALSE(
      PlayerAdTreatmentScriptFor(GURL("http://localhost:3000/")).empty());
  EXPECT_TRUE(PlayerAdTreatmentScriptFor(GURL("chrome://settings")).empty());
  EXPECT_TRUE(PlayerAdTreatmentScriptFor(GURL("file:///tmp/a.html")).empty());
  EXPECT_TRUE(PlayerAdTreatmentScriptFor(GURL()).empty());
}

TEST(PlayerAdTreatmentTest, ScriptIsIdenticalEverywhereAndInstallsOnce) {
  const std::string a = PlayerAdTreatmentScriptFor(GURL("https://a.example/"));
  const std::string b =
      PlayerAdTreatmentScriptFor(GURL("https://b.example/watch"));
  // One generic engine, not per-site variants: the same bytes everywhere is
  // what makes this reviewable as a single vetted script.
  EXPECT_EQ(a, b);
  // Reinstallation guard, because the agent can inject on soft navigations.
  EXPECT_NE(a.find("__seoulPlayerAdTreatment"), std::string::npos);
}

// The pattern table is behavior, so its rows are pinned: losing one silently
// would stop treating that player's ads with nothing else failing.
TEST(PlayerAdTreatmentTest, CoversTheMajorPlayerSdks) {
  const std::string script =
      PlayerAdTreatmentScriptFor(GURL("https://example.com/"));
  EXPECT_NE(script.find(".html5-video-player.ad-showing"), std::string::npos)
      << "YouTube's player";
  EXPECT_NE(script.find(".ima-ad-container"), std::string::npos)
      << "Google IMA, the common publisher SDK";
  EXPECT_NE(script.find(".jw-flag-ads"), std::string::npos) << "JW Player";
  EXPECT_NE(script.find(".vjs-ad-playing"), std::string::npos) << "video.js";
  // The two treatment verbs.
  EXPECT_NE(script.find("currentTime = video.duration"), std::string::npos);
  EXPECT_NE(script.find("button.click()"), std::string::npos);
  // And the guard that keeps it off content video: seeks are scoped.
  EXPECT_NE(script.find("scope.querySelector('video')"), std::string::npos);
}

}  // namespace
}  // namespace seoul::adblock
