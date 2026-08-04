// Project Seoul filter-subscription downloader tests.

#include "seoul/browser/adblock/ad_block_subscription_downloader.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "crypto/sha2.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

std::string Sha256Hex(std::string_view contents) {
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(contents)));
}

network::mojom::URLResponseHeadPtr TextResponseHead(
    std::string_view content_type = "text/plain") {
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 200 OK\nContent-Type: " + std::string(content_type) + "\n\n");
  head->mime_type = std::string(content_type);
  return head;
}

class AdBlockSubscriptionDownloaderTest : public testing::Test {
 protected:
  AdBlockSubscriptionDownloaderTest() {
    factory_.SetInterceptor(
        base::BindRepeating(&AdBlockSubscriptionDownloaderTest::CaptureRequest,
                            base::Unretained(this)));
  }

  void AddResponse(const GURL& url,
                   std::string_view body,
                   std::string_view content_type = "text/plain") {
    network::URLLoaderCompletionStatus status;
    status.decoded_body_length = body.size();
    factory_.AddResponse(url, TextResponseHead(content_type), body, status);
  }

  void CaptureRequest(const network::ResourceRequest& request) {
    captured_request_ = request;
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_{true};
  std::optional<network::ResourceRequest> captured_request_;
};

TEST_F(AdBlockSubscriptionDownloaderTest,
       DownloadsBoundedCredentiallessHttpsAndVerifiesHash) {
  const GURL url("https://filters.example/list.txt");
  const std::string rules = "||ads.example^\n";
  AddResponse(url, rules);
  AdBlockSubscriptionDownloader downloader(factory_.GetSafeWeakWrapper());

  base::test::TestFuture<AdBlockSubscriptionDownloadResult> downloaded;
  downloader.Download(url, Sha256Hex(rules), downloaded.GetCallback());
  const AdBlockSubscriptionDownloadResult result = downloaded.Get();
  EXPECT_TRUE(result.success) << result.error;
  EXPECT_EQ(rules, result.rules);
  EXPECT_TRUE(result.error.empty());

  ASSERT_TRUE(captured_request_);
  const network::ResourceRequest& request = *captured_request_;
  EXPECT_EQ("GET", request.method);
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  EXPECT_TRUE(request.load_flags & net::LOAD_BYPASS_CACHE);
  EXPECT_TRUE(request.load_flags & net::LOAD_DISABLE_CACHE);
  EXPECT_TRUE(request.load_flags & net::LOAD_DO_NOT_SAVE_COOKIES);
}

TEST_F(AdBlockSubscriptionDownloaderTest, RejectsHttpBeforeNetworkAccess) {
  AdBlockSubscriptionDownloader downloader(factory_.GetSafeWeakWrapper());
  base::test::TestFuture<AdBlockSubscriptionDownloadResult> downloaded;
  downloader.Download(GURL("http://filters.example/list.txt"),
                      std::string(64, '0'), downloaded.GetCallback());
  const AdBlockSubscriptionDownloadResult result = downloaded.Get();
  EXPECT_FALSE(result.success);
  EXPECT_NE(std::string::npos, result.error.find("HTTPS"));
  EXPECT_TRUE(factory_.pending_requests()->empty());
}

TEST_F(AdBlockSubscriptionDownloaderTest,
       RejectsBodyThatDoesNotMatchTrustedHash) {
  const GURL url("https://filters.example/list.txt");
  AddResponse(url, "||tampered.example^\n");
  AdBlockSubscriptionDownloader downloader(factory_.GetSafeWeakWrapper());
  base::test::TestFuture<AdBlockSubscriptionDownloadResult> downloaded;
  downloader.Download(url, Sha256Hex("||expected.example^\n"),
                      downloaded.GetCallback());
  const AdBlockSubscriptionDownloadResult result = downloaded.Get();
  EXPECT_FALSE(result.success);
  EXPECT_NE(std::string::npos, result.error.find("SHA-256 mismatch"))
      << result.error;
}

TEST_F(AdBlockSubscriptionDownloaderTest, RejectsUnsupportedContentType) {
  const GURL url("https://filters.example/list.txt");
  const std::string body = "<html>not a filter list</html>";
  AddResponse(url, body, "text/html");
  AdBlockSubscriptionDownloader downloader(factory_.GetSafeWeakWrapper());
  base::test::TestFuture<AdBlockSubscriptionDownloadResult> downloaded;
  downloader.Download(url, Sha256Hex(body), downloaded.GetCallback());
  const AdBlockSubscriptionDownloadResult result = downloaded.Get();
  EXPECT_FALSE(result.success);
  EXPECT_NE(std::string::npos, result.error.find("content type"));
}

TEST_F(AdBlockSubscriptionDownloaderTest, RejectsNonSuccessStatus) {
  const GURL url("https://filters.example/list.txt");
  factory_.AddResponse(url.spec(), "unavailable",
                       net::HTTP_SERVICE_UNAVAILABLE);
  AdBlockSubscriptionDownloader downloader(factory_.GetSafeWeakWrapper());
  base::test::TestFuture<AdBlockSubscriptionDownloadResult> downloaded;
  downloader.Download(url, Sha256Hex("unavailable"), downloaded.GetCallback());
  const AdBlockSubscriptionDownloadResult result = downloaded.Get();
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.find("network error") != std::string::npos ||
              result.error.find("HTTP 200") != std::string::npos);
}

}  // namespace
}  // namespace seoul::adblock
