// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_PAGE_HANDLER_H_
#define SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_PAGE_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "seoul/browser/onboarding/welcome.mojom.h"

class Profile;
class BrowserWindowInterface;

namespace seoul {

// Serves the first-run surface.
//
// Holds no state of its own. Every value it reports is read at the moment it is
// asked for, from the thing that owns it - prefs for step progress, the blocker
// for what is loaded, the platform for default-browser status, the window's
// state controller for the rail. Caching any of it here would produce a screen
// that disagrees with the browser behind it, and the browser is right.
class SeoulWelcomePageHandler : public welcome::mojom::PageHandler {
 public:
  SeoulWelcomePageHandler(
      mojo::PendingReceiver<welcome::mojom::PageHandler> receiver,
      Profile* profile,
      BrowserWindowInterface* browser_window);
  SeoulWelcomePageHandler(const SeoulWelcomePageHandler&) = delete;
  SeoulWelcomePageHandler& operator=(const SeoulWelcomePageHandler&) = delete;
  ~SeoulWelcomePageHandler() override;

  // welcome::mojom::PageHandler:
  void GetState(GetStateCallback callback) override;
  void CompleteStep(const std::string& step_id,
                    CompleteStepCallback callback) override;
  void Skip() override;
  void SetRailCollapsed(bool collapsed) override;
  void RequestDefaultBrowser(RequestDefaultBrowserCallback callback) override;

 private:
  welcome::mojom::WelcomeStatePtr BuildState() const;

  mojo::Receiver<welcome::mojom::PageHandler> receiver_;
  // The profile outlives the WebUI that owns this handler.
  const raw_ptr<Profile> profile_;
  // The hosting window. Null is tolerated everywhere it is used: a WebUI can
  // outlive its window during teardown, and the rail controls are simply
  // inert then rather than a crash on the first-run screen.
  const raw_ptr<BrowserWindowInterface> browser_window_;

  base::WeakPtrFactory<SeoulWelcomePageHandler> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_PAGE_HANDLER_H_
