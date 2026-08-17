// Project Seoul top-level navigation blocker throttle.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_NAVIGATION_THROTTLE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_NAVIGATION_THROTTLE_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/navigation_throttle.h"
#include "seoul/browser/adblock/ad_block_decision.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "url/gurl.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace seoul::adblock {

// What the throttle needs to know about the frame it is guarding. Captured
// once at construction: a top-frame navigation destroys the subframe and its
// throttle, so the outermost URL cannot shift underneath a redirect chain.
struct AdBlockNavigationContext {
  bool is_subframe = false;
  GURL outermost_top_frame_url;
  std::optional<content::GlobalRenderFrameHostToken> parent_frame_token;
};

class AdBlockNavigationThrottle : public content::NavigationThrottle {
 public:
  using DecisionCallback = base::OnceCallback<void(AdBlockDecision)>;
  using CheckRequestCallback =
      base::RepeatingCallback<void(AdBlockRequest, DecisionCallback)>;

  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  AdBlockNavigationThrottle(content::NavigationThrottleRegistry& registry,
                            CheckRequestCallback check_request,
                            AdBlockNavigationContext context = {});
  AdBlockNavigationThrottle(const AdBlockNavigationThrottle&) = delete;
  AdBlockNavigationThrottle& operator=(const AdBlockNavigationThrottle&) =
      delete;
  ~AdBlockNavigationThrottle() override;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

  void set_restart_callback_for_testing(
      base::RepeatingCallback<void(const GURL&)> callback) {
    restart_callback_for_testing_ = std::move(callback);
  }

 private:
  ThrottleCheckResult CheckCurrentUrl();
  void OnDecision(AdBlockDecision decision);
  void ScheduleRestartNavigation(const GURL& url);

  CheckRequestCallback check_request_;
  const AdBlockNavigationContext context_;
  base::RepeatingCallback<void(const GURL&)> restart_callback_for_testing_;
  bool callback_ran_ = false;
  bool is_deferred_ = false;
  ThrottleCheckResult immediate_result_{PROCEED};
  base::WeakPtrFactory<AdBlockNavigationThrottle> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_NAVIGATION_THROTTLE_H_
