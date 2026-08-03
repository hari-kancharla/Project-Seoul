// Project Seoul bounded HTTPS downloader for hash-pinned filter subscriptions.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SUBSCRIPTION_DOWNLOADER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SUBSCRIPTION_DOWNLOADER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace net {
struct RedirectInfo;
}  // namespace net

namespace network {
class SharedURLLoaderFactory;
namespace mojom {
class URLResponseHead;
}  // namespace mojom
}  // namespace network

namespace seoul::adblock {

struct AdBlockSubscriptionDownloadResult {
  AdBlockSubscriptionDownloadResult();
  AdBlockSubscriptionDownloadResult(const AdBlockSubscriptionDownloadResult&);
  AdBlockSubscriptionDownloadResult& operator=(
      const AdBlockSubscriptionDownloadResult&);
  AdBlockSubscriptionDownloadResult(AdBlockSubscriptionDownloadResult&&);
  AdBlockSubscriptionDownloadResult& operator=(
      AdBlockSubscriptionDownloadResult&&);
  ~AdBlockSubscriptionDownloadResult();

  bool success = false;
  std::string rules;
  std::string error;
};

// Uses the profile's browser-process URLLoaderFactory, which is deliberately
// outside Seoul's renderer/document/worker interceptor chain. Redirects are
// rejected, credentials are omitted, response bytes are bounded, and the body
// is accepted only when it matches a caller-supplied trusted SHA-256.
class AdBlockSubscriptionDownloader {
 public:
  using CompletionCallback =
      base::OnceCallback<void(AdBlockSubscriptionDownloadResult)>;

  explicit AdBlockSubscriptionDownloader(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~AdBlockSubscriptionDownloader();

  AdBlockSubscriptionDownloader(const AdBlockSubscriptionDownloader&) = delete;
  AdBlockSubscriptionDownloader& operator=(
      const AdBlockSubscriptionDownloader&) = delete;

  void Download(const GURL& url,
                std::string expected_sha256,
                CompletionCallback callback);
  bool is_downloading() const { return simple_url_loader_ != nullptr; }

 private:
  void OnRedirect(const GURL& url_before_redirect,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* to_be_removed_headers);
  void OnDownloaded(std::optional<std::string> body);
  void Finish(AdBlockSubscriptionDownloadResult result);

  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;
  std::string expected_sha256_;
  CompletionCallback callback_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SUBSCRIPTION_DOWNLOADER_H_
