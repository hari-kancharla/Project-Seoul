// Project Seoul `$csp` response filtering for document navigations.

#include "seoul/browser/adblock/ad_block_csp_throttle.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "url/origin.h"

namespace seoul::adblock {
namespace {

constexpr char kContentSecurityPolicyHeader[] = "Content-Security-Policy";

// Only frame navigations carry `$csp`. Subresource destinations (script, image,
// stylesheet, font, xhr, websocket, ...) never reach the engine's CSP path, so
// they are filtered out before a throttle is even created.
bool IsDocumentDestination(network::mojom::RequestDestination destination,
                           bool* is_main_frame) {
  switch (destination) {
    case network::mojom::RequestDestination::kDocument:
      *is_main_frame = true;
      return true;
    case network::mojom::RequestDestination::kIframe:
    case network::mojom::RequestDestination::kFrame:
      *is_main_frame = false;
      return true;
    default:
      return false;
  }
}

}  // namespace

// static
std::unique_ptr<AdBlockCspThrottle> AdBlockCspThrottle::MaybeCreate(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter) {
  bool is_main_frame = false;
  if (!IsDocumentDestination(request.destination, &is_main_frame)) {
    return nullptr;
  }
  if (!request.url.SchemeIsHTTPOrHTTPS()) {
    return nullptr;
  }

  Profile* const profile = Profile::FromBrowserContext(browser_context);
  AdBlockService* const service =
      profile ? AdBlockServiceFactory::GetForProfile(profile) : nullptr;
  if (!service) {
    return nullptr;
  }

  // Shields are keyed by the top-level site, so a subframe is governed by its
  // embedder. The main frame is its own top-level site and is resolved from the
  // response URL instead, which keeps redirects correct.
  GURL top_frame_url;
  if (!is_main_frame && wc_getter) {
    if (content::WebContents* const contents = wc_getter.Run()) {
      top_frame_url = contents->GetLastCommittedURL();
    }
  }

  return std::make_unique<AdBlockCspThrottle>(
      service->GetWeakPtr(), is_main_frame, std::move(top_frame_url));
}

AdBlockCspThrottle::AdBlockCspThrottle(base::WeakPtr<AdBlockService> service,
                                       bool is_main_frame,
                                       GURL top_frame_url)
    : service_(std::move(service)),
      is_main_frame_(is_main_frame),
      top_frame_url_(std::move(top_frame_url)) {}

AdBlockCspThrottle::~AdBlockCspThrottle() = default;

const char* AdBlockCspThrottle::NameForLoggingWillProcessResponse() {
  return "AdBlockCspThrottle";
}

void AdBlockCspThrottle::WillProcessResponse(
    const GURL& response_url,
    network::mojom::URLResponseHead* response_head,
    bool* defer) {
  // `response_url` is the final URL after any redirect chain, so a rule that
  // matches only the last hop is evaluated against that hop.
  if (!service_ || !response_head || !response_head->headers ||
      !response_head->parsed_headers ||
      !response_url.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  // The site whose mode governs injection: the embedder for a subframe, the
  // document itself for a main frame.
  const GURL& site_url =
      (!is_main_frame_ && top_frame_url_.is_valid() &&
       top_frame_url_.SchemeIsHTTPOrHTTPS())
          ? top_frame_url_
          : response_url;

  AdBlockRequest request;
  request.url = response_url.spec();
  request.hostname = response_url.host();
  request.source_hostname = site_url.host();
  request.request_type = is_main_frame_ ? "main_frame" : "sub_frame";
  request.is_third_party =
      !is_main_frame_ &&
      IsThirdPartyRequest(response_url, url::Origin::Create(site_url),
                          /*initiator_origin=*/std::nullopt);
  request.outermost_top_frame_url = site_url.spec();

  response_head_ = response_head;
  deferred_response_url_ = response_url;

  // The service answers synchronously whenever it can decide without the
  // engine - Off for this site, or a non-eligible URL - and asynchronously
  // otherwise. Resuming a loader that has not yet recorded the deferral drops
  // the resume and hangs the navigation, so the deferral is only declared once
  // the callback is known not to have run inline.
  result_received_ = false;
  service_->GetCspDirectives(
      std::move(request), site_url,
      base::BindOnce(&AdBlockCspThrottle::OnCspDirectives,
                     weak_factory_.GetWeakPtr()));

  if (!result_received_) {
    *defer = true;
    deferred_ = true;
  }
}

void AdBlockCspThrottle::OnCspDirectives(std::string directives) {
  // Destruction of this throttle cancels the callback, so a cancelled
  // navigation or a closed tab never lands here. `deferred_` distinguishes the
  // inline answer (still inside WillProcessResponse, nothing to resume) from
  // the asynchronous one.
  result_received_ = true;
  const bool was_deferred = deferred_;
  deferred_ = false;

  network::mojom::URLResponseHead* const response_head = response_head_;
  const GURL response_url = deferred_response_url_;
  response_head_ = nullptr;
  deferred_response_url_ = GURL();

  if (!directives.empty() && response_head && response_head->headers &&
      response_head->parsed_headers) {
    // Parse first: an unparseable directive string must not reach the page as a
    // malformed header, even though the engine is expected to sanitize.
    std::vector<network::mojom::ContentSecurityPolicyPtr> parsed =
        network::ParseContentSecurityPolicies(
            directives, network::mojom::ContentSecurityPolicyType::kEnforce,
            network::mojom::ContentSecurityPolicySource::kHTTP, response_url);
    // CSP parsing is lenient: a string such as ";;;" yields a policy object
    // carrying no directives at all. Such a policy enforces nothing, so
    // requiring at least one real directive keeps a junk header off the wire.
    const bool has_directive =
        std::ranges::any_of(parsed, [](const auto& policy) {
          return !policy->directives.empty() ||
                 !policy->raw_directives.empty();
        });
    if (has_directive) {
      // Enforcement path: Chromium reads navigation CSP from `parsed_headers`.
      // Appending leaves the site's own policies - enforced and report-only -
      // exactly as delivered.
      for (auto& policy : parsed) {
        response_head->parsed_headers->content_security_policy.push_back(
            std::move(policy));
      }
      // Observability path: an added header, never a replaced one.
      response_head->headers->AddHeader(kContentSecurityPolicyHeader,
                                        directives);
    }
  }

  // Exactly one Resume per defer, and never for an inline answer.
  if (was_deferred && delegate_) {
    delegate_->Resume();
  }
}

}  // namespace seoul::adblock
