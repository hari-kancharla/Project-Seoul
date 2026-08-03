// Project Seoul top-level navigation blocker throttle.

#include "seoul/browser/adblock/ad_block_navigation_throttle.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "ui/base/page_transition_types.h"

namespace seoul::adblock {
namespace {

void CheckWithService(base::WeakPtr<AdBlockService> service,
                      AdBlockRequest request,
                      AdBlockNavigationThrottle::DecisionCallback callback) {
  if (!service) {
    std::move(callback).Run(AdBlockDecision());
    return;
  }
  service->CheckRequest(std::move(request), std::move(callback));
}

}  // namespace

// static
void AdBlockNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame() || !handle.GetWebContents()) {
    return;
  }

  Profile* profile =
      Profile::FromBrowserContext(handle.GetWebContents()->GetBrowserContext());
  AdBlockService* service =
      profile ? AdBlockServiceFactory::GetForProfile(profile) : nullptr;
  if (!service) {
    return;
  }

  registry.AddThrottle(std::make_unique<AdBlockNavigationThrottle>(
      registry, base::BindRepeating(&CheckWithService, service->GetWeakPtr())));
}

AdBlockNavigationThrottle::AdBlockNavigationThrottle(
    content::NavigationThrottleRegistry& registry,
    CheckRequestCallback check_request)
    : content::NavigationThrottle(registry),
      check_request_(std::move(check_request)) {
  CHECK(check_request_);
}

AdBlockNavigationThrottle::~AdBlockNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
AdBlockNavigationThrottle::WillStartRequest() {
  return CheckCurrentUrl();
}

content::NavigationThrottle::ThrottleCheckResult
AdBlockNavigationThrottle::WillRedirectRequest() {
  return CheckCurrentUrl();
}

const char* AdBlockNavigationThrottle::GetNameForLogging() {
  return "AdBlockNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
AdBlockNavigationThrottle::CheckCurrentUrl() {
  const GURL& url = navigation_handle()->GetURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }

  callback_ran_ = false;
  is_deferred_ = false;
  immediate_result_ = ThrottleCheckResult(PROCEED);

  std::string method = navigation_handle()->GetRequestMethod();
  if (method.empty()) {
    method = "GET";
  }
  check_request_.Run(
      BuildNavigationAdBlockRequest(
          url, navigation_handle()->GetInitiatorOrigin(), std::move(method)),
      base::BindOnce(&AdBlockNavigationThrottle::OnDecision,
                     weak_factory_.GetWeakPtr()));
  if (callback_ran_) {
    return immediate_result_;
  }

  is_deferred_ = true;
  return DEFER;
}

void AdBlockNavigationThrottle::OnDecision(AdBlockDecision decision) {
  callback_ran_ = true;
  const bool should_block = decision.action == AdBlockAction::kBlock;
  std::optional<GURL> rewritten_url;
  if (decision.action == AdBlockAction::kRewrite &&
      decision.rewritten_url) {
    const GURL candidate(*decision.rewritten_url);
    std::string method = navigation_handle()->GetRequestMethod();
    if (method.empty()) {
      method = "GET";
    }
    if (IsSafeAdBlockUrlRewrite(navigation_handle()->GetURL(), candidate,
                                method)) {
      rewritten_url = candidate;
    }
  }
  if (rewritten_url) {
    immediate_result_ = ThrottleCheckResult(CANCEL);
    ScheduleRestartNavigation(*rewritten_url);
    if (!is_deferred_) {
      return;
    }
    is_deferred_ = false;
    // CancelDeferredNavigation() may synchronously delete this throttle.
    CancelDeferredNavigation(ThrottleCheckResult(CANCEL));
    return;
  }

  immediate_result_ = should_block ? ThrottleCheckResult(BLOCK_REQUEST)
                                   : ThrottleCheckResult(PROCEED);
  if (!is_deferred_) {
    return;
  }

  is_deferred_ = false;
  if (should_block) {
    CancelDeferredNavigation(
        ThrottleCheckResult(CANCEL, net::ERR_BLOCKED_BY_CLIENT));
    return;
  }

  // Resume() may synchronously delete this throttle.
  Resume();
}

void AdBlockNavigationThrottle::ScheduleRestartNavigation(const GURL& url) {
  if (restart_callback_for_testing_) {
    restart_callback_for_testing_.Run(url);
    return;
  }

  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* web_contents = handle->GetWebContents();
  if (!web_contents) {
    return;
  }
  content::OpenURLParams params =
      content::OpenURLParams::FromNavigationHandle(handle);
  params.url = url;
  params.transition = static_cast<ui::PageTransition>(
      params.transition | ui::PAGE_TRANSITION_CLIENT_REDIRECT);
  params.redirect_chain.clear();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> weak_web_contents,
             content::OpenURLParams params) {
            if (weak_web_contents) {
              weak_web_contents->OpenURL(
                  params, /*navigation_handle_callback=*/{});
            }
          },
          web_contents->GetWeakPtr(), std::move(params)));
}

}  // namespace seoul::adblock
