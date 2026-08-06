// Project Seoul `$csp` response filtering for document navigations.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CSP_THROTTLE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CSP_THROTTLE_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace network {
struct ResourceRequest;
}  // namespace network

namespace seoul::adblock {

class AdBlockService;

// Applies `$csp` rules to eligible document and subdocument responses.
//
// Chromium consumes navigation CSP from `URLResponseHead::parsed_headers`, not
// from the raw header block, so this throttle updates both: the raw header for
// observability and the parsed list for enforcement. The generated policy is
// always *appended* as an additional policy and never replaces, rewrites, or
// removes the site's own `Content-Security-Policy` or its report-only header.
// Appending is the only combination CSP defines - the user agent enforces every
// delivered policy - so an injected directive can restrict but never relax.
//
// Rule matching lives entirely in AdBlockService, which remains the authority
// for Off/Standard/Aggressive, per-site overrides, exceptions, and both engine
// groups. This class contributes no policy of its own.
class AdBlockCspThrottle : public blink::URLLoaderThrottle {
 public:
  // Returns null when the request is not an eligible frame navigation or the
  // profile has no blocker, so non-document loads pay nothing.
  static std::unique_ptr<AdBlockCspThrottle> MaybeCreate(
      const network::ResourceRequest& request,
      content::BrowserContext* browser_context,
      const base::RepeatingCallback<content::WebContents*()>& wc_getter);

  AdBlockCspThrottle(base::WeakPtr<AdBlockService> service,
                     bool is_main_frame,
                     GURL top_frame_url);
  AdBlockCspThrottle(const AdBlockCspThrottle&) = delete;
  AdBlockCspThrottle& operator=(const AdBlockCspThrottle&) = delete;
  ~AdBlockCspThrottle() override;

  // blink::URLLoaderThrottle:
  void WillProcessResponse(const GURL& response_url,
                           network::mojom::URLResponseHead* response_head,
                           bool* defer) override;
  const char* NameForLoggingWillProcessResponse() override;

 private:
  void OnCspDirectives(std::string directives);

  const base::WeakPtr<AdBlockService> service_;
  const bool is_main_frame_;
  const GURL top_frame_url_;

  // Valid only while the response is deferred. ThrottlingURLLoader owns this
  // throttle, so the loader outliving the head is not possible: destruction
  // cancels the pending callback through `weak_factory_`.
  raw_ptr<network::mojom::URLResponseHead> response_head_ = nullptr;
  GURL deferred_response_url_;
  bool deferred_ = false;
  // Set by the callback; lets WillProcessResponse detect an inline answer.
  bool result_received_ = false;

  base::WeakPtrFactory<AdBlockCspThrottle> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CSP_THROTTLE_H_
