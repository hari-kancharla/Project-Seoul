// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ONBOARDING_ONBOARDING_STATE_H_
#define SEOUL_BROWSER_ONBOARDING_ONBOARDING_STATE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace seoul::onboarding {

// The ordered steps of first run.
//
// Deliberately short. Zen's setup asks about browser layout, theme, import,
// default browser, Essentials and Spaces; asking all of that before a person
// has seen the browser work is a questionnaire, not an introduction. These
// three cover what changes the browser's appearance and behaviour on the spot,
// and everything else is reachable from Studio afterwards.
enum class Step {
  // What Seoul is, in one screen.
  kWelcome = 0,
  // Theme and the rail's resting width. Both apply live, so the choice is
  // visible in the window behind the surface as it is made.
  kAppearance = 1,
  // Default-browser offer, and what the blocker is already doing.
  kBrowsing = 2,
};

// Every step in order. The single source for iteration and for bounds.
std::vector<Step> AllSteps();

// Stable string form, used as the pref value and on the wire. Never change one
// once shipped: a renamed step reads as "not completed" and re-onboards
// everybody who had finished it.
std::string_view StepId(Step step);
std::optional<Step> StepFromId(std::string_view id);

// Why onboarding is or is not being shown. Carried rather than reduced to a
// bool so the decision can be logged and tested by its reason, not just its
// outcome.
enum class Decision {
  // Fresh profile, nothing recorded: show it.
  kShowFirstRun,
  // Started and abandoned: resume at the first incomplete step rather than
  // restarting from the top or skipping what is left.
  kResume,
  // Finished, or explicitly skipped.
  kAlreadyDone,
  // A profile that predates onboarding. Someone who has been using Seoul must
  // not be handed a welcome flow by an update; absence of the pref is not
  // evidence of a new user, only of an old build.
  kExistingProfile,
};

// Decides what first run should do for this profile.
//
// `profile_has_prior_seoul_state` is the caller's answer to "has this profile
// been used before" - the presence of workspaces, boosts, or any other Seoul
// state written by a previous session. It is passed in rather than looked up
// so this stays free of service dependencies and testable on its own.
Decision Decide(const PrefService* prefs, bool profile_has_prior_seoul_state);

// The step to show now, or nullopt when nothing should be shown.
std::optional<Step> NextStep(const PrefService* prefs,
                             bool profile_has_prior_seoul_state);

// Records that `step` finished. Idempotent: completing a step twice is not an
// error, because a renderer can and does send the same completion twice when a
// page is restored.
void MarkStepComplete(PrefService* prefs, Step step);

// Ends onboarding without completing the remaining steps. Distinct from
// completion so the two can be told apart in metrics and in support: a person
// who skipped saw nothing, and a person who finished chose their settings.
void MarkSkipped(PrefService* prefs);

// True when every step is complete, or the flow was skipped.
bool IsFinished(const PrefService* prefs);

// Marks a pre-existing profile as not needing onboarding, so the decision is
// recorded once rather than re-derived from state that may later be cleared.
void MarkExistingProfileOnboarded(PrefService* prefs);

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Pref names, exposed for tests and for the settings surface.
inline constexpr char kCompletedStepsPref[] = "seoul.onboarding.completed_steps";
inline constexpr char kSkippedPref[] = "seoul.onboarding.skipped";

}  // namespace seoul::onboarding

#endif  // SEOUL_BROWSER_ONBOARDING_ONBOARDING_STATE_H_
