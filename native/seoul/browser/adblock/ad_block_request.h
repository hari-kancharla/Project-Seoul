// Project Seoul native blocker request model.
// Carries browser-trusted request metadata into the single-sequence matcher.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_H_

#include <optional>
#include <string>

#include "content/public/browser/global_routing_id.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/origin.h"

namespace seoul::adblock {

enum class AdBlockMode {
  kOff,
  kStandard,
  kAggressive,
};

enum class AdBlockFactoryType {
  kDocumentSubresource,
  kWorkerMainResource,
  kWorkerSubresource,
  kServiceWorkerScript,
  kServiceWorkerSubresource,
  kWebSocket,
  kNavigation,
};

struct AdBlockRequest {
  AdBlockRequest();
  AdBlockRequest(std::string url,
                 std::string hostname,
                 std::string source_hostname,
                 std::string request_type,
                 bool is_third_party);
  AdBlockRequest(const AdBlockRequest&);
  AdBlockRequest& operator=(const AdBlockRequest&);
  AdBlockRequest(AdBlockRequest&&);
  AdBlockRequest& operator=(AdBlockRequest&&);
  ~AdBlockRequest();

  std::string url;
  std::string hostname;
  std::string source_hostname;
  std::string request_type;
  bool is_third_party = false;

  std::string initiator_url;
  std::string outermost_top_frame_url;
  int blink_resource_type = -1;
  std::string method = "GET";
  AdBlockMode mode = AdBlockMode::kStandard;
  AdBlockFactoryType factory_type = AdBlockFactoryType::kDocumentSubresource;
  std::optional<content::GlobalRenderFrameHostToken> render_frame_token;
  std::optional<std::string> devtools_request_id;
  bool originated_from_service_worker = false;
};

// Maps Chromium's detailed resource metadata to adblock-rust's stable request
// strings. Unknown values intentionally map to "other".
std::string AdBlockRequestTypeFor(
    int blink_resource_type,
    network::mojom::RequestDestination destination);

// Uses registry-controlled domains, including private registries. Missing or
// opaque first-party context fails open as first party.
bool IsThirdPartyRequest(const GURL& request_url,
                         const std::optional<url::Origin>& top_frame_origin,
                         const std::optional<url::Origin>& initiator_origin);

bool IsSupportedRequestScheme(const GURL& url);

// Accepts only an HTTP(S) rewrite that preserves every URL component except
// for removing existing query key/value pairs, and only for idempotent methods.
bool IsSafeAdBlockUrlRewrite(const GURL& original_url,
                             const GURL& rewritten_url,
                             std::string_view method);

// Replaces the request URL while recomputing all URL-derived matcher fields.
// The source hostname and other browser-trusted context remain unchanged.
bool RewriteAdBlockRequestUrl(AdBlockRequest* request, const GURL& new_url);

AdBlockRequest BuildAdBlockRequest(
    const network::ResourceRequest& request,
    const std::optional<url::Origin>& top_frame_origin,
    std::optional<content::GlobalRenderFrameHostToken> render_frame_token,
    AdBlockFactoryType factory_type);

AdBlockRequest BuildWebSocketAdBlockRequest(
    const GURL& url,
    const std::optional<url::Origin>& top_frame_origin,
    const std::optional<url::Origin>& initiator_origin,
    std::optional<content::GlobalRenderFrameHostToken> render_frame_token);

AdBlockRequest BuildNavigationAdBlockRequest(
    const GURL& url,
    const std::optional<url::Origin>& initiator_origin,
    std::string method);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_H_
