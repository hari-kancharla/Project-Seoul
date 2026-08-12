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

// How the downloader decides a body is trustworthy.
enum class AdBlockSubscriptionIntegrity {
  // The body must hash to a SHA-256 the caller already knows. Use this for
  // anything whose exact bytes were agreed in advance - a pinned rule set named
  // by signed catalog metadata, for instance.
  kPinnedSha256,

  // Transport integrity only: HTTPS, no redirects, no credentials, bounded
  // size, HTTP 200, a text content type, and valid UTF-8 - but no content hash.
  //
  // This exists because a catalogued upstream list has no stable hash to pin:
  // EasyList changes whenever its maintainers publish, several times a day, so
  // a pinned hash would either be wrong within hours or would freeze the list
  // at one revision forever. Every blocker that consumes these lists is in the
  // same position and resolves it the same way, by trusting HTTPS to the
  // maintainers' own origin. The rules are still not trusted blindly: the
  // filter-list manager validates the text and must construct a working engine
  // from it before anything is swapped in, and a failure at any point retains
  // the previously active engines.
  kCataloguedHttps,
};

// Uses the profile's browser-process URLLoaderFactory, which is deliberately
// outside Seoul's renderer/document/worker interceptor chain. Redirects are
// rejected, credentials are omitted, and response bytes are bounded. Whether
// the body must also match a known hash is the caller's choice, per
// AdBlockSubscriptionIntegrity.
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

  // Pinned form, unchanged.
  void Download(const GURL& url,
                std::string expected_sha256,
                CompletionCallback callback);

  // Explicit form. `expected_sha256` is ignored for kCataloguedHttps.
  void Download(const GURL& url,
                AdBlockSubscriptionIntegrity integrity,
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
  AdBlockSubscriptionIntegrity integrity_ =
      AdBlockSubscriptionIntegrity::kPinnedSha256;
  std::string expected_sha256_;
  CompletionCallback callback_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SUBSCRIPTION_DOWNLOADER_H_
