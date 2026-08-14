// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/onboarding/onboarding_state.h"

#include <algorithm>

#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace seoul::onboarding {

namespace {

bool HasCompleted(const PrefService* prefs, Step step) {
  if (!prefs) {
    return false;
  }
  const base::ListValue& completed = prefs->GetList(kCompletedStepsPref);
  const std::string_view id = StepId(step);
  return std::ranges::any_of(completed, [&id](const base::Value& value) {
    return value.is_string() && value.GetString() == id;
  });
}

bool AnyStepCompleted(const PrefService* prefs) {
  if (!prefs) {
    return false;
  }
  return !prefs->GetList(kCompletedStepsPref).empty();
}

bool WasSkipped(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kSkippedPref);
}

}  // namespace

std::vector<Step> AllSteps() {
  return {Step::kWelcome, Step::kAppearance, Step::kBrowsing};
}

std::string_view StepId(Step step) {
  switch (step) {
    case Step::kWelcome:
      return "welcome";
    case Step::kAppearance:
      return "appearance";
    case Step::kBrowsing:
      return "browsing";
  }
  return "";
}

std::optional<Step> StepFromId(std::string_view id) {
  for (Step step : AllSteps()) {
    if (StepId(step) == id) {
      return step;
    }
  }
  return std::nullopt;
}

Decision Decide(const PrefService* prefs, bool profile_has_prior_seoul_state) {
  if (WasSkipped(prefs) || IsFinished(prefs)) {
    return Decision::kAlreadyDone;
  }
  // Order matters: a profile part-way through onboarding also has Seoul state,
  // so resumption has to be checked before the pre-existing-profile rule or an
  // abandoned run would be silently written off as an upgrade.
  if (AnyStepCompleted(prefs)) {
    return Decision::kResume;
  }
  if (profile_has_prior_seoul_state) {
    return Decision::kExistingProfile;
  }
  return Decision::kShowFirstRun;
}

std::optional<Step> NextStep(const PrefService* prefs,
                             bool profile_has_prior_seoul_state) {
  const Decision decision = Decide(prefs, profile_has_prior_seoul_state);
  if (decision == Decision::kAlreadyDone ||
      decision == Decision::kExistingProfile) {
    return std::nullopt;
  }
  for (Step step : AllSteps()) {
    if (!HasCompleted(prefs, step)) {
      return step;
    }
  }
  return std::nullopt;
}

void MarkStepComplete(PrefService* prefs, Step step) {
  if (!prefs || HasCompleted(prefs, step)) {
    return;
  }
  ScopedListPrefUpdate update(prefs, kCompletedStepsPref);
  update->Append(std::string(StepId(step)));
}

void MarkSkipped(PrefService* prefs) {
  if (prefs) {
    prefs->SetBoolean(kSkippedPref, true);
  }
}

bool IsFinished(const PrefService* prefs) {
  if (WasSkipped(prefs)) {
    return true;
  }
  const std::vector<Step> steps = AllSteps();
  return std::ranges::all_of(
      steps, [prefs](Step step) { return HasCompleted(prefs, step); });
}

void MarkExistingProfileOnboarded(PrefService* prefs) {
  // Recorded as skipped rather than as every step completed: the person did
  // not make these choices, and a later "what did they pick" question must not
  // be answered with defaults they never saw.
  MarkSkipped(prefs);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kCompletedStepsPref);
  registry->RegisterBooleanPref(kSkippedPref, false);
}

}  // namespace seoul::onboarding
