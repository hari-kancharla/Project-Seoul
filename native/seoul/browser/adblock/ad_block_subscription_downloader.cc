// Project Seoul bounded HTTPS downloader for hash-pinned filter subscriptions.

#include "seoul/browser/adblock/ad_block_subscription_downloader.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "crypto/sha2.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/url_constants.h"

namespace seoul::adblock {
namespace {

constexpr size_t kMaxSubscriptionBytes =
    network::SimpleURLLoader::kMaxBoundedStringDownloadSize;
constexpr base::TimeDelta kDownloadTimeout = base::Seconds(30);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("seoul_adblock_filter_subscription",
                                        R"(
      semantics {
        sender: "Seoul native content blocker"
        description:
          "Downloads a user-selected or browser-vetted content-blocking "
          "filter list. The response is accepted only when its SHA-256 "
          "matches trusted metadata already held by the browser."
        trigger:
          "A scheduled blocker update or an explicit user request."
        data: "No user data. The request contains only the filter-list URL."
        destination: WEBSITE
        internal {
          contacts {
            owners: "//seoul/browser/adblock/OWNERS"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-07-29"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Filter-list downloads occur only for lists enabled by the browser "
          "or user. Disabling the list stops its downloads."
        policy_exception_justification:
          "No enterprise policy is defined until subscription settings ship."
      })");

bool IsValidSha256(std::string_view digest) {
  if (digest.size() != crypto::kSHA256Length * 2) {
    return false;
  }
  return std::ranges::all_of(
      digest, [](char character) { return base::IsHexDigit(character); });
}

std::string Sha256Hex(std::string_view contents) {
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(contents)));
}

}  // namespace

AdBlockSubscriptionDownloadResult::AdBlockSubscriptionDownloadResult() =
    default;
AdBlockSubscriptionDownloadResult::AdBlockSubscriptionDownloadResult(
    const AdBlockSubscriptionDownloadResult&) = default;
AdBlockSubscriptionDownloadResult& AdBlockSubscriptionDownloadResult::operator=(
    const AdBlockSubscriptionDownloadResult&) = default;
AdBlockSubscriptionDownloadResult::AdBlockSubscriptionDownloadResult(
    AdBlockSubscriptionDownloadResult&&) = default;
AdBlockSubscriptionDownloadResult& AdBlockSubscriptionDownloadResult::operator=(
    AdBlockSubscriptionDownloadResult&&) = default;
AdBlockSubscriptionDownloadResult::~AdBlockSubscriptionDownloadResult() =
    default;

AdBlockSubscriptionDownloader::AdBlockSubscriptionDownloader(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

AdBlockSubscriptionDownloader::~AdBlockSubscriptionDownloader() = default;

void AdBlockSubscriptionDownloader::Download(const GURL& url,
                                             std::string expected_sha256,
                                             CompletionCallback callback) {
  AdBlockSubscriptionDownloadResult error;
  if (is_downloading()) {
    error.error = "a filter-list download is already in progress";
    std::move(callback).Run(std::move(error));
    return;
  }
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
    error.error = "filter-list URL must use HTTPS";
    std::move(callback).Run(std::move(error));
    return;
  }
  if (!IsValidSha256(expected_sha256)) {
    error.error = "trusted filter-list SHA-256 is invalid";
    std::move(callback).Run(std::move(error));
    return;
  }

  expected_sha256_ = base::ToLowerASCII(expected_sha256);
  callback_ = std::move(callback);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE |
                        net::LOAD_DO_NOT_SAVE_COOKIES;
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "text/plain, application/octet-stream;q=0.9");

  simple_url_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  simple_url_loader_->SetTimeoutDuration(kDownloadTimeout);
  simple_url_loader_->SetRetryOptions(
      /*max_retries=*/0, network::SimpleURLLoader::RETRY_NEVER);
  simple_url_loader_->SetOnRedirectCallback(base::BindRepeating(
      &AdBlockSubscriptionDownloader::OnRedirect, base::Unretained(this)));
  simple_url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&AdBlockSubscriptionDownloader::OnDownloaded,
                     base::Unretained(this)),
      kMaxSubscriptionBytes);
}

void AdBlockSubscriptionDownloader::OnRedirect(
    const GURL& url_before_redirect,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* to_be_removed_headers) {
  AdBlockSubscriptionDownloadResult result;
  result.error = "filter-list redirects are not allowed";
  Finish(std::move(result));
}

void AdBlockSubscriptionDownloader::OnDownloaded(
    std::optional<std::string> body) {
  AdBlockSubscriptionDownloadResult result;
  if (!simple_url_loader_) {
    return;
  }
  if (simple_url_loader_->NetError() != net::OK) {
    result.error = "filter-list network error: " +
                   base::NumberToString(simple_url_loader_->NetError());
    Finish(std::move(result));
    return;
  }
  const network::mojom::URLResponseHead* response =
      simple_url_loader_->ResponseInfo();
  if (!response || !response->headers ||
      response->headers->response_code() != net::HTTP_OK) {
    result.error = "filter-list response was not HTTP 200";
    Finish(std::move(result));
    return;
  }
  std::string mime_type = response->mime_type;
  if (mime_type.empty()) {
    response->headers->GetMimeType(&mime_type);
  }
  if (mime_type != "text/plain" && mime_type != "application/octet-stream") {
    result.error = "filter-list response has an unsupported content type";
    Finish(std::move(result));
    return;
  }
  if (!body || body->empty()) {
    result.error = "filter-list response is empty";
    Finish(std::move(result));
    return;
  }
  if (body->find('\0') != std::string::npos || !base::IsStringUTF8(*body)) {
    result.error = "filter-list response is not valid UTF-8 text";
    Finish(std::move(result));
    return;
  }
  if (Sha256Hex(*body) != expected_sha256_) {
    result.error = "filter-list response SHA-256 mismatch";
    Finish(std::move(result));
    return;
  }

  result.success = true;
  result.rules = std::move(*body);
  Finish(std::move(result));
}

void AdBlockSubscriptionDownloader::Finish(
    AdBlockSubscriptionDownloadResult result) {
  simple_url_loader_.reset();
  expected_sha256_.clear();
  if (callback_) {
    std::move(callback_).Run(std::move(result));
  }
}

}  // namespace seoul::adblock
