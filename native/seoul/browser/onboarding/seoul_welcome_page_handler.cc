// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/onboarding/seoul_welcome_page_handler.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "components/prefs/pref_service.h"
#include "seoul/browser/adblock/ad_block_filter_list_manager.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/onboarding/onboarding_state.h"

namespace seoul {

namespace {

// Reported verbatim to the page. Deliberately the same words the readiness
// report uses, so a screenshot of the first-run screen and the report cannot
// disagree about what is loaded.
std::string FilterSourceName(adblock::AdBlockFilterListSource source) {
  switch (source) {
    case adblock::AdBlockFilterListSource::kNone:
      return "none";
    case adblock::AdBlockFilterListSource::kBundled:
      return "bundled";
    case adblock::AdBlockFilterListSource::kCache:
      return "cache";
    case adblock::AdBlockFilterListSource::kVerifiedComponent:
      return "verified-component";
    case adblock::AdBlockFilterListSource::kPinnedSubscription:
      return "pinned";
    case adblock::AdBlockFilterListSource::kCatalogueSubscription:
      return "catalogue";
  }
  return "none";
}

tabs::VerticalTabStripStateController* RailController(
    BrowserWindowInterface* window) {
  return window ? tabs::VerticalTabStripStateController::From(window) : nullptr;
}

}  // namespace

SeoulWelcomePageHandler::SeoulWelcomePageHandler(
    mojo::PendingReceiver<welcome::mojom::PageHandler> receiver,
    Profile* profile,
    BrowserWindowInterface* browser_window)
    : receiver_(this, std::move(receiver)),
      profile_(profile),
      browser_window_(browser_window) {}

SeoulWelcomePageHandler::~SeoulWelcomePageHandler() = default;

welcome::mojom::WelcomeStatePtr SeoulWelcomePageHandler::BuildState() const {
  auto state = welcome::mojom::WelcomeState::New();

  PrefService* const prefs = profile_ ? profile_->GetPrefs() : nullptr;
  // The welcome surface is only ever reached on a first run, so the profile is
  // by definition not a pre-existing one; passing false here keeps the surface
  // usable if it is opened directly by url, which is how it is tested.
  const std::optional<onboarding::Step> step =
      onboarding::NextStep(prefs, /*profile_has_prior_seoul_state=*/false);
  state->current_step = step ? std::string(onboarding::StepId(*step)) : "";

  for (onboarding::Step all : onboarding::AllSteps()) {
    state->all_steps.push_back(std::string(onboarding::StepId(all)));
  }
  if (prefs) {
    for (const base::Value& value : prefs->GetList(
             onboarding::kCompletedStepsPref)) {
      if (value.is_string()) {
        state->completed_steps.push_back(value.GetString());
      }
    }
  }

  // Default-browser status, asked of the platform rather than remembered.
  state->can_set_default_browser = shell_integration::CanSetAsDefaultBrowser();
  state->is_default_browser =
      shell_integration::GetDefaultBrowser() == shell_integration::IS_DEFAULT;

  // Blocking, as measured.
  adblock::AdBlockService* const blocker =
      profile_ ? adblock::AdBlockServiceFactory::GetForProfile(profile_)
               : nullptr;
  if (blocker) {
    const adblock::AdBlockFilterListUpdateStatus status =
        blocker->filter_list_status();
    // "Enabled" here means the engine actually has rules loaded, not that a
    // preference is set to on. A blocker whose lists failed to load is off in
    // every way the user can observe, and the first-run screen should say so
    // rather than report the preference and be wrong.
    state->blocking_enabled =
        status.state == adblock::AdBlockFilterListState::kReady;
    state->filter_source = FilterSourceName(status.source);
  } else {
    state->blocking_enabled = false;
    state->filter_source = "none";
  }

  const tabs::VerticalTabStripStateController* const rail =
      browser_window_ ? tabs::VerticalTabStripStateController::From(
                            const_cast<BrowserWindowInterface*>(
                                browser_window_.get()))
                      : nullptr;
  state->rail_collapsed = rail && rail->IsCollapsed();

  return state;
}

void SeoulWelcomePageHandler::GetState(GetStateCallback callback) {
  std::move(callback).Run(BuildState());
}

void SeoulWelcomePageHandler::CompleteStep(const std::string& step_id,
                                           CompleteStepCallback callback) {
  // An unknown id is ignored rather than treated as an error: a renderer from a
  // newer or older build must not be able to wedge the flow, and the state
  // returned below tells it what the browser actually believes.
  if (const std::optional<onboarding::Step> step =
          onboarding::StepFromId(step_id)) {
    onboarding::MarkStepComplete(profile_ ? profile_->GetPrefs() : nullptr,
                                 *step);
  }
  std::move(callback).Run(BuildState());
}

void SeoulWelcomePageHandler::Skip() {
  onboarding::MarkSkipped(profile_ ? profile_->GetPrefs() : nullptr);
}

void SeoulWelcomePageHandler::SetRailCollapsed(bool collapsed) {
  tabs::VerticalTabStripStateController* const rail =
      RailController(browser_window_);
  if (!rail) {
    return;
  }
  // Compact is expand-on-hover plus collapsed, which is what the toolbar's own
  // compact control does. Collapsing alone produces the sixty-DIP icon rail,
  // not the five-DIP edge - CollapsedRegionWidth() only returns the compact
  // width when expand-on-hover is enabled - so the first-run screen was
  // offering "Compact" and delivering something else.
  rail->SetExpandOnHoverEnabledForWindow(collapsed);
  rail->RequestCollapse(collapsed);
}

void SeoulWelcomePageHandler::RequestDefaultBrowser(
    RequestDefaultBrowserCallback callback) {
  if (!shell_integration::CanSetAsDefaultBrowser()) {
    std::move(callback).Run(false);
    return;
  }
  // The worker owns the platform conversation, which on macOS shows a system
  // prompt the user can refuse. The reply is whatever the platform ends up
  // saying, not whether the request was made.
  scoped_refptr<shell_integration::DefaultBrowserWorker> worker =
      base::MakeRefCounted<shell_integration::DefaultBrowserWorker>();
  worker->StartSetAsDefault(base::BindOnce(
      [](RequestDefaultBrowserCallback done,
         shell_integration::DefaultWebClientState result) {
        std::move(done).Run(result == shell_integration::IS_DEFAULT);
      },
      std::move(callback)));
}

}  // namespace seoul
