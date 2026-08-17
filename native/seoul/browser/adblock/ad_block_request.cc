// Project Seoul native blocker request model.

#include "seoul/browser/adblock/ad_block_request.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"

namespace seoul::adblock {
namespace {

std::optional<GURL> FirstPartyUrl(
    const std::optional<url::Origin>& top_frame_origin,
    const std::optional<url::Origin>& initiator_origin) {
  if (top_frame_origin && !top_frame_origin->opaque()) {
    return top_frame_origin->GetURL();
  }
  if (initiator_origin && !initiator_origin->opaque()) {
    return initiator_origin->GetURL();
  }
  return std::nullopt;
}

}  // namespace

AdBlockRequest::AdBlockRequest() = default;

AdBlockRequest::AdBlockRequest(std::string url,
                               std::string hostname,
                               std::string source_hostname,
                               std::string request_type,
                               bool is_third_party)
    : url(std::move(url)),
      hostname(std::move(hostname)),
      source_hostname(std::move(source_hostname)),
      request_type(std::move(request_type)),
      is_third_party(is_third_party) {}

AdBlockRequest::AdBlockRequest(const AdBlockRequest&) = default;
AdBlockRequest& AdBlockRequest::operator=(const AdBlockRequest&) = default;
AdBlockRequest::AdBlockRequest(AdBlockRequest&&) = default;
AdBlockRequest& AdBlockRequest::operator=(AdBlockRequest&&) = default;
AdBlockRequest::~AdBlockRequest() = default;

std::string AdBlockRequestTypeFor(
    int blink_resource_type,
    network::mojom::RequestDestination destination) {
  switch (static_cast<blink::mojom::ResourceType>(blink_resource_type)) {
    case blink::mojom::ResourceType::kMainFrame:
    case blink::mojom::ResourceType::kNavigationPreloadMainFrame:
      return "main_frame";
    case blink::mojom::ResourceType::kSubFrame:
    case blink::mojom::ResourceType::kNavigationPreloadSubFrame:
      return "sub_frame";
    case blink::mojom::ResourceType::kStylesheet:
      return "stylesheet";
    case blink::mojom::ResourceType::kScript:
    case blink::mojom::ResourceType::kWorker:
    case blink::mojom::ResourceType::kSharedWorker:
    case blink::mojom::ResourceType::kServiceWorker:
    case blink::mojom::ResourceType::kJson:
      return "script";
    case blink::mojom::ResourceType::kImage:
    case blink::mojom::ResourceType::kFavicon:
      return "image";
    case blink::mojom::ResourceType::kFontResource:
      return "font";
    case blink::mojom::ResourceType::kMedia:
      return "media";
    case blink::mojom::ResourceType::kObject:
    case blink::mojom::ResourceType::kPluginResource:
      return "object";
    case blink::mojom::ResourceType::kXhr:
      return "xmlhttprequest";
    case blink::mojom::ResourceType::kPing:
      return "ping";
    case blink::mojom::ResourceType::kCspReport:
      return "csp_report";
    case blink::mojom::ResourceType::kSubResource:
    case blink::mojom::ResourceType::kPrefetch:
      break;
  }

  switch (destination) {
    case network::mojom::RequestDestination::kDocument:
      return "main_frame";
    case network::mojom::RequestDestination::kFrame:
    case network::mojom::RequestDestination::kIframe:
    case network::mojom::RequestDestination::kFencedframe:
      return "sub_frame";
    case network::mojom::RequestDestination::kStyle:
      return "stylesheet";
    case network::mojom::RequestDestination::kScript:
    case network::mojom::RequestDestination::kServiceWorker:
    case network::mojom::RequestDestination::kSharedWorker:
    case network::mojom::RequestDestination::kWorker:
    case network::mojom::RequestDestination::kAudioWorklet:
    case network::mojom::RequestDestination::kPaintWorklet:
    case network::mojom::RequestDestination::kSharedStorageWorklet:
    case network::mojom::RequestDestination::kJson:
    case network::mojom::RequestDestination::kSpeculationRules:
      return "script";
    case network::mojom::RequestDestination::kImage:
      return "image";
    case network::mojom::RequestDestination::kFont:
      return "font";
    case network::mojom::RequestDestination::kAudio:
    case network::mojom::RequestDestination::kTrack:
    case network::mojom::RequestDestination::kVideo:
      return "media";
    case network::mojom::RequestDestination::kEmbed:
    case network::mojom::RequestDestination::kObject:
      return "object";
    case network::mojom::RequestDestination::kReport:
      return "ping";
    case network::mojom::RequestDestination::kXslt:
      return "xslt";
    case network::mojom::RequestDestination::kEmpty:
      return "xmlhttprequest";
    case network::mojom::RequestDestination::kManifest:
    case network::mojom::RequestDestination::kWebBundle:
    case network::mojom::RequestDestination::kWebIdentity:
    case network::mojom::RequestDestination::kDictionary:
    case network::mojom::RequestDestination::kEmailVerification:
      return "other";
  }
  return "other";
}

bool IsThirdPartyRequest(const GURL& request_url,
                         const std::optional<url::Origin>& top_frame_origin,
                         const std::optional<url::Origin>& initiator_origin) {
  const std::optional<GURL> first_party_url =
      FirstPartyUrl(top_frame_origin, initiator_origin);
  if (!request_url.is_valid() || !first_party_url ||
      !first_party_url->is_valid()) {
    return false;
  }
  return !net::registry_controlled_domains::SameDomainOrHost(
      request_url, *first_party_url,
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

bool IsSupportedRequestScheme(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("ws") || url.SchemeIs("wss");
}

bool IsSafeAdBlockUrlRewrite(const GURL& original_url,
                             const GURL& rewritten_url,
                             std::string_view method) {
  if (method != "GET" && method != "HEAD" && method != "OPTIONS") {
    return false;
  }
  if (!original_url.is_valid() || !original_url.SchemeIsHTTPOrHTTPS() ||
      !rewritten_url.is_valid() || !rewritten_url.SchemeIsHTTPOrHTTPS() ||
      original_url.scheme() != rewritten_url.scheme() ||
      original_url.username() != rewritten_url.username() ||
      original_url.password() != rewritten_url.password() ||
      original_url.host() != rewritten_url.host() ||
      original_url.EffectiveIntPort() != rewritten_url.EffectiveIntPort() ||
      original_url.path() != rewritten_url.path() ||
      original_url.ref() != rewritten_url.ref()) {
    return false;
  }

  std::vector<std::pair<std::string, std::string>> original_parameters;
  for (net::QueryIterator it(original_url); !it.IsAtEnd(); it.Advance()) {
    original_parameters.emplace_back(it.GetKey(), it.GetValue());
  }
  for (net::QueryIterator it(rewritten_url); !it.IsAtEnd(); it.Advance()) {
    const std::pair<std::string, std::string> parameter(it.GetKey(),
                                                        it.GetValue());
    const auto found =
        std::ranges::find(original_parameters, parameter);
    if (found == original_parameters.end()) {
      return false;
    }
    original_parameters.erase(found);
  }
  return true;
}

bool RewriteAdBlockRequestUrl(AdBlockRequest* request, const GURL& new_url) {
  if (!request || !new_url.is_valid() || !new_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  request->url = new_url.spec();
  request->hostname = new_url.host();
  if (request->source_hostname.empty()) {
    request->is_third_party = false;
    return true;
  }

  const std::string request_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          request->hostname,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string source_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          request->source_hostname,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  request->is_third_party =
      request->hostname != request->source_hostname &&
      (request_domain.empty() || source_domain.empty() ||
       request_domain != source_domain);
  return true;
}

AdBlockRequest BuildAdBlockRequest(
    const network::ResourceRequest& request,
    const std::optional<url::Origin>& top_frame_origin,
    std::optional<content::GlobalRenderFrameHostToken> render_frame_token,
    AdBlockFactoryType factory_type) {
  AdBlockRequest result;
  result.url = request.url.spec();
  result.hostname = request.url.host();
  result.blink_resource_type = request.resource_type;
  result.request_type =
      AdBlockRequestTypeFor(request.resource_type, request.destination);
  result.method = request.method;
  result.factory_type = factory_type;
  result.render_frame_token = std::move(render_frame_token);
  result.devtools_request_id = request.devtools_request_id;
  result.originated_from_service_worker =
      request.originated_from_service_worker;

  if (request.request_initiator && !request.request_initiator->opaque()) {
    result.initiator_url = request.request_initiator->GetURL().spec();
  }
  if (top_frame_origin && !top_frame_origin->opaque()) {
    result.outermost_top_frame_url = top_frame_origin->GetURL().spec();
  }

  const std::optional<GURL> first_party_url =
      FirstPartyUrl(top_frame_origin, request.request_initiator);
  if (first_party_url) {
    result.source_hostname = first_party_url->host();
  }
  result.is_third_party = IsThirdPartyRequest(request.url, top_frame_origin,
                                              request.request_initiator);
  return result;
}

AdBlockRequest BuildWebSocketAdBlockRequest(
    const GURL& url,
    const std::optional<url::Origin>& top_frame_origin,
    const std::optional<url::Origin>& initiator_origin,
    std::optional<content::GlobalRenderFrameHostToken> render_frame_token) {
  AdBlockRequest result;
  result.url = url.spec();
  result.hostname = url.host();
  result.request_type = "websocket";
  result.method = "GET";
  result.factory_type = AdBlockFactoryType::kWebSocket;
  result.render_frame_token = std::move(render_frame_token);

  if (initiator_origin && !initiator_origin->opaque()) {
    result.initiator_url = initiator_origin->GetURL().spec();
  }
  if (top_frame_origin && !top_frame_origin->opaque()) {
    result.outermost_top_frame_url = top_frame_origin->GetURL().spec();
  }

  const std::optional<GURL> first_party_url =
      FirstPartyUrl(top_frame_origin, initiator_origin);
  if (first_party_url) {
    result.source_hostname = first_party_url->host();
  }
  result.is_third_party =
      IsThirdPartyRequest(url, top_frame_origin, initiator_origin);
  return result;
}

AdBlockRequest BuildNavigationAdBlockRequest(
    const GURL& url,
    const std::optional<url::Origin>& initiator_origin,
    std::string method) {
  AdBlockRequest result;
  result.url = url.spec();
  result.hostname = url.host();
  result.source_hostname = url.host();
  result.request_type = "main_frame";
  result.method = std::move(method);
  result.factory_type = AdBlockFactoryType::kNavigation;
  result.outermost_top_frame_url = url.spec();

  if (initiator_origin && !initiator_origin->opaque()) {
    result.initiator_url = initiator_origin->GetURL().spec();
  }
  return result;
}

AdBlockRequest BuildSubFrameNavigationAdBlockRequest(
    const GURL& url,
    const GURL& outermost_top_frame_url,
    const std::optional<url::Origin>& initiator_origin,
    std::string method) {
  AdBlockRequest result;
  result.url = url.spec();
  result.hostname = url.host();
  result.request_type = "sub_frame";
  result.method = std::move(method);
  result.factory_type = AdBlockFactoryType::kNavigation;

  std::optional<url::Origin> top_frame_origin;
  if (outermost_top_frame_url.is_valid() &&
      outermost_top_frame_url.SchemeIsHTTPOrHTTPS()) {
    top_frame_origin = url::Origin::Create(outermost_top_frame_url);
    // Per-site settings key off this, so it has to be the page the user is
    // looking at. Pointing it at the frame's own URL would make "turn blocking
    // off for this site" look up the ad network instead of the site.
    result.outermost_top_frame_url = outermost_top_frame_url.spec();
  }
  if (initiator_origin && !initiator_origin->opaque()) {
    result.initiator_url = initiator_origin->GetURL().spec();
  }
  if (const std::optional<GURL> first_party =
          FirstPartyUrl(top_frame_origin, initiator_origin)) {
    result.source_hostname = first_party->host();
  }
  result.is_third_party =
      IsThirdPartyRequest(url, top_frame_origin, initiator_origin);
  return result;
}

}  // namespace seoul::adblock
