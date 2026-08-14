// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/onboarding/onboarding_state.h"

#include <memory>

#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::onboarding {
namespace {

class OnboardingStateTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_ = std::make_unique<
        sync_preferences::TestingPrefServiceSyncable>();
    RegisterProfilePrefs(prefs_->registry());
  }

  PrefService* prefs() { return prefs_.get(); }

  std::unique_ptr<sync_preferences::TestingPrefServiceSyncable> prefs_;
};

TEST_F(OnboardingStateTest, FreshProfileStartsAtTheFirstStep) {
  EXPECT_EQ(Decision::kShowFirstRun,
            Decide(prefs(), /*profile_has_prior_seoul_state=*/false));
  EXPECT_EQ(Step::kWelcome,
            NextStep(prefs(), /*profile_has_prior_seoul_state=*/false));
  EXPECT_FALSE(IsFinished(prefs()));
}

// The rule that matters most on an update: someone who has been using Seoul
// must not be handed a welcome flow because a new build introduced one. The
// absence of the pref says the build is new, not that the user is.
TEST_F(OnboardingStateTest, ExistingProfileIsNeverOnboardedByAnUpdate) {
  EXPECT_EQ(Decision::kExistingProfile,
            Decide(prefs(), /*profile_has_prior_seoul_state=*/true));
  EXPECT_EQ(std::nullopt,
            NextStep(prefs(), /*profile_has_prior_seoul_state=*/true));
}

// ...but a run that was abandoned half-way also has Seoul state, so resumption
// has to win over the upgrade rule or the remaining steps vanish.
TEST_F(OnboardingStateTest, AbandonedRunResumesEvenWithPriorState) {
  MarkStepComplete(prefs(), Step::kWelcome);
  EXPECT_EQ(Decision::kResume,
            Decide(prefs(), /*profile_has_prior_seoul_state=*/true));
  EXPECT_EQ(Step::kAppearance,
            NextStep(prefs(), /*profile_has_prior_seoul_state=*/true));
}

TEST_F(OnboardingStateTest, StepsAdvanceInOrderAndThenFinish) {
  EXPECT_EQ(Step::kWelcome, NextStep(prefs(), false));
  MarkStepComplete(prefs(), Step::kWelcome);
  EXPECT_EQ(Step::kAppearance, NextStep(prefs(), false));
  MarkStepComplete(prefs(), Step::kAppearance);
  EXPECT_EQ(Step::kBrowsing, NextStep(prefs(), false));
  MarkStepComplete(prefs(), Step::kBrowsing);

  EXPECT_TRUE(IsFinished(prefs()));
  EXPECT_EQ(std::nullopt, NextStep(prefs(), false));
  EXPECT_EQ(Decision::kAlreadyDone, Decide(prefs(), false));
}

// A restored page re-sends the completion it already sent. Completing twice
// must not append twice, or "all steps complete" starts depending on how many
// times a renderer happened to fire.
TEST_F(OnboardingStateTest, CompletingAStepTwiceIsHarmless) {
  MarkStepComplete(prefs(), Step::kWelcome);
  MarkStepComplete(prefs(), Step::kWelcome);
  EXPECT_EQ(1u, prefs()->GetList(kCompletedStepsPref).size());
  EXPECT_EQ(Step::kAppearance, NextStep(prefs(), false));
}

TEST_F(OnboardingStateTest, SkipEndsTheFlowWithoutCompletingSteps) {
  MarkSkipped(prefs());
  EXPECT_TRUE(IsFinished(prefs()));
  EXPECT_EQ(Decision::kAlreadyDone, Decide(prefs(), false));
  EXPECT_EQ(std::nullopt, NextStep(prefs(), false));
  // Skipping is not the same as having chosen every default.
  EXPECT_TRUE(prefs()->GetList(kCompletedStepsPref).empty());
}

TEST_F(OnboardingStateTest, MarkingAnExistingProfileRecordsTheDecision) {
  MarkExistingProfileOnboarded(prefs());
  // Recorded, so clearing unrelated Seoul state later cannot make the browser
  // decide this is a new profile and onboard someone mid-use.
  EXPECT_EQ(Decision::kAlreadyDone,
            Decide(prefs(), /*profile_has_prior_seoul_state=*/false));
}

// Step ids are persisted, so a rename reads as "not completed" and re-onboards
// everyone who had finished. Pin them.
TEST_F(OnboardingStateTest, StepIdsAreStableAndRoundTrip) {
  EXPECT_EQ("welcome", StepId(Step::kWelcome));
  EXPECT_EQ("appearance", StepId(Step::kAppearance));
  EXPECT_EQ("browsing", StepId(Step::kBrowsing));

  for (Step step : AllSteps()) {
    EXPECT_EQ(step, StepFromId(StepId(step)));
  }
  EXPECT_EQ(std::nullopt, StepFromId("not-a-step"));
  EXPECT_EQ(3u, AllSteps().size());
}

// An unknown id in the pref - a downgrade after a later build added a step -
// must not crash or count towards completion.
TEST_F(OnboardingStateTest, UnknownPersistedStepIsIgnored) {
  {
    ScopedListPrefUpdate update(prefs(), kCompletedStepsPref);
    update->Append("a-step-from-the-future");
  }

  EXPECT_FALSE(IsFinished(prefs()));
  EXPECT_EQ(Step::kWelcome, NextStep(prefs(), false));
  EXPECT_EQ(Decision::kResume, Decide(prefs(), false));
}

}  // namespace
}  // namespace seoul::onboarding
