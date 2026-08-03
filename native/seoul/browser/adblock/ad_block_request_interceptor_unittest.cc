// Project Seoul URLLoaderFactory interceptor tests.

#include "seoul/browser/adblock/ad_block_request_interceptor.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "content/public/test/browser_task_environment.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/redirect_info.h"
#include "seoul/browser/adblock/ad_block_resource_catalog.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"

namespace seoul::adblock {
namespace {

class AdBlockRequestInterceptorTest : public testing::Test {
 public:
  using ResponseFuture = base::test::TestFuture<std::optional<std::string>>;

  AdBlockRequestInterceptorTest() : terminal_receiver_(&terminal_factory_) {}
  ~AdBlockRequestInterceptorTest() override = default;

  void StartRequest(
      AdBlockRequestInterceptor::CheckRequestCallback check_request,
      std::unique_ptr<network::ResourceRequest> request) {
    network::URLLoaderFactoryBuilder factory_builder;
    interceptor_ = std::make_unique<AdBlockRequestInterceptor>(
        std::move(check_request),
        url::Origin::Create(GURL("https://news.example/")), std::nullopt,
        AdBlockFactoryType::kDocumentSubresource, factory_builder,
        base::BindOnce([](AdBlockRequestInterceptor*) {
          // The fixture owns the test interceptor.
        }));

    factory_remote_.Bind(
        std::move(factory_builder)
            .Finish<mojo::PendingRemote<network::mojom::URLLoaderFactory>>(
                terminal_receiver_.BindNewPipeAndPassRemote()));
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               TRAFFIC_ANNOTATION_FOR_TESTS);
    loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
        factory_remote_.get(), response_future_.GetCallback());
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  network::TestURLLoaderFactory terminal_factory_{true};
  mojo::Receiver<network::mojom::URLLoaderFactory> terminal_receiver_;
  std::unique_ptr<AdBlockRequestInterceptor> interceptor_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_remote_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  ResponseFuture response_future_;
};

TEST_F(AdBlockRequestInterceptorTest, AllowsRequestAndPreservesResponse) {
  const GURL url("https://cdn.example/app.js");
  terminal_factory_.AddResponse(url.spec(), "allowed body");
  int checks = 0;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(base::BindRepeating(
                   [](int* checks, AdBlockRequest request,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     ++*checks;
                     EXPECT_EQ("script", request.request_type);
                     std::move(callback).Run(AdBlockDecision());
                   },
                   &checks),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::OK, loader_->NetError());
  ASSERT_TRUE(response_future_.Get());
  EXPECT_EQ("allowed body", *response_future_.Get());
  EXPECT_EQ(1, checks);
}

TEST_F(AdBlockRequestInterceptorTest,
       BlocksBeforeRequestReachesNetworkFactory) {
  const GURL url("https://ads.example/banner.js");
  int checks = 0;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(base::BindRepeating(
                   [](int* checks, AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     ++*checks;
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kBlock;
                     decision.matched = true;
                     std::move(callback).Run(std::move(decision));
                   },
                   &checks),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, loader_->NetError());
  EXPECT_EQ(0, terminal_factory_.NumPending());
  EXPECT_EQ(1, checks);
}

TEST_F(AdBlockRequestInterceptorTest,
       RequestFrameFlagCannotBypassSubresourceFactory) {
  const GURL url("https://ads.example/frame-flag.js");
  int checks = 0;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;
  request->is_outermost_main_frame = true;

  StartRequest(base::BindRepeating(
                   [](int* checks, AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     ++*checks;
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kBlock;
                     decision.matched = true;
                     std::move(callback).Run(std::move(decision));
                   },
                   &checks),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, loader_->NetError());
  EXPECT_EQ(0, terminal_factory_.NumPending());
  EXPECT_EQ(1, checks);
}

TEST_F(AdBlockRequestInterceptorTest,
       ServesVettedRedirectWithoutReachingNetworkFactory) {
  const GURL url("https://ads.example/noop.js");
  const std::vector<AdBlockResource> catalog = GetAdBlockResourceCatalog();
  const AdBlockResource& resource = catalog.front();
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(base::BindRepeating(
                   [](std::string data_url, AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kRedirect;
                     decision.matched = true;
                     decision.redirect = std::move(data_url);
                     std::move(callback).Run(std::move(decision));
                   },
                   resource.data_url),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::OK, loader_->NetError());
  ASSERT_TRUE(response_future_.Get());
  EXPECT_EQ(resource.body, *response_future_.Get());
  EXPECT_EQ(0, terminal_factory_.NumPending());
}

TEST_F(AdBlockRequestInterceptorTest, RejectsUnvettedRedirectData) {
  const GURL url("https://ads.example/arbitrary.js");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(base::BindRepeating(
                   [](AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kRedirect;
                     decision.matched = true;
                     decision.redirect =
                         "data:application/javascript;base64,YXJiaXRyYXJ5";
                     std::move(callback).Run(std::move(decision));
                   }),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, loader_->NetError());
  EXPECT_EQ(0, terminal_factory_.NumPending());
}

TEST_F(AdBlockRequestInterceptorTest, AppliesSafeUrlRewriteBeforeNetwork) {
  const GURL original_url("https://news.example/api?id=7&utm=tracking");
  const GURL rewritten_url("https://news.example/api?id=7");
  terminal_factory_.AddResponse(rewritten_url.spec(), "rewritten body");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = original_url;
  request->method = "GET";
  request->resource_type = static_cast<int>(blink::mojom::ResourceType::kXhr);
  request->destination = network::mojom::RequestDestination::kEmpty;

  StartRequest(base::BindRepeating(
                   [](std::string rewritten_url, AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kRewrite;
                     decision.rewritten_url = std::move(rewritten_url);
                     std::move(callback).Run(std::move(decision));
                   },
                   rewritten_url.spec()),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::OK, loader_->NetError());
  ASSERT_TRUE(response_future_.Get());
  EXPECT_EQ("rewritten body", *response_future_.Get());
  EXPECT_EQ(0, terminal_factory_.NumPending());
}

TEST_F(AdBlockRequestInterceptorTest,
       InvalidRewriteFallsBackToOriginalRequest) {
  const GURL original_url("https://news.example/api?id=7");
  terminal_factory_.AddResponse(original_url.spec(), "original body");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = original_url;
  request->method = "POST";
  request->resource_type = static_cast<int>(blink::mojom::ResourceType::kXhr);
  request->destination = network::mojom::RequestDestination::kEmpty;

  StartRequest(base::BindRepeating(
                   [](AdBlockRequest,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     AdBlockDecision decision;
                     decision.action = AdBlockAction::kRewrite;
                     decision.rewritten_url = "https://other.example/api";
                     std::move(callback).Run(std::move(decision));
                   }),
               std::move(request));

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::OK, loader_->NetError());
  ASSERT_TRUE(response_future_.Get());
  EXPECT_EQ("original body", *response_future_.Get());
}

TEST_F(AdBlockRequestInterceptorTest, RechecksAndBlocksRedirectTarget) {
  const GURL initial_url("https://origin.example/start");
  const GURL redirect_url("https://ads.example/redirected.js");
  net::RedirectInfo redirect_info;
  redirect_info.status_code = 302;
  redirect_info.new_method = "GET";
  redirect_info.new_url = redirect_url;

  int checks = 0;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = initial_url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(
      base::BindRepeating(
          [](int* checks, const GURL& redirect_url, AdBlockRequest request,
             AdBlockRequestInterceptor::DecisionCallback callback) {
            ++*checks;
            AdBlockDecision decision;
            if (request.url == redirect_url.spec()) {
              decision.action = AdBlockAction::kBlock;
              decision.matched = true;
            }
            std::move(callback).Run(std::move(decision));
          },
          &checks, redirect_url),
      std::move(request));

  terminal_factory_.WaitForRequest(initial_url);
  network::TestURLLoaderFactory::PendingRequest* terminal_request =
      terminal_factory_.GetPendingRequest(0);
  ASSERT_TRUE(terminal_request);
  ASSERT_TRUE(terminal_request->client.is_connected());
  terminal_request->client->OnReceiveRedirect(
      redirect_info, network::mojom::URLResponseHead::New());
  terminal_request->client.FlushForTesting();
  task_environment_.RunUntilIdle();
  ASSERT_EQ(2, checks);

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, loader_->NetError());
  EXPECT_EQ(2, checks);
}

TEST_F(AdBlockRequestInterceptorTest,
       RechecksRedirectTargetAndServesVettedReplacement) {
  const GURL initial_url("https://origin.example/start");
  const GURL redirect_url("https://ads.example/noop.js");
  const std::vector<AdBlockResource> catalog = GetAdBlockResourceCatalog();
  const AdBlockResource& resource = catalog.front();
  net::RedirectInfo redirect_info;
  redirect_info.status_code = 302;
  redirect_info.new_method = "GET";
  redirect_info.new_url = redirect_url;

  int checks = 0;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = initial_url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(base::BindRepeating(
                   [](int* checks, const GURL& redirect_url,
                      std::string data_url, AdBlockRequest request,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
                     ++*checks;
                     AdBlockDecision decision;
                     if (request.url == redirect_url.spec()) {
                       decision.action = AdBlockAction::kRedirect;
                       decision.matched = true;
                       decision.redirect = std::move(data_url);
                     }
                     std::move(callback).Run(std::move(decision));
                   },
                   &checks, redirect_url, resource.data_url),
               std::move(request));

  terminal_factory_.WaitForRequest(initial_url);
  network::TestURLLoaderFactory::PendingRequest* terminal_request =
      terminal_factory_.GetPendingRequest(0);
  ASSERT_TRUE(terminal_request);
  terminal_request->client->OnReceiveRedirect(
      redirect_info, network::mojom::URLResponseHead::New());
  terminal_request->client.FlushForTesting();

  ASSERT_TRUE(response_future_.Wait());
  EXPECT_EQ(net::OK, loader_->NetError());
  ASSERT_TRUE(response_future_.Get());
  EXPECT_EQ(resource.body, *response_future_.Get());
  EXPECT_EQ(2, checks);
}

TEST_F(AdBlockRequestInterceptorTest,
       LateDecisionAfterClientClosesDoesNotUseFreedRequest) {
  const GURL url("https://cdn.example/delayed.js");
  std::optional<AdBlockRequestInterceptor::DecisionCallback> delayed_decision;
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->resource_type =
      static_cast<int>(blink::mojom::ResourceType::kScript);
  request->destination = network::mojom::RequestDestination::kScript;

  StartRequest(
      base::BindRepeating(
          [](std::optional<AdBlockRequestInterceptor::DecisionCallback>*
                 delayed_decision,
             AdBlockRequest,
             AdBlockRequestInterceptor::DecisionCallback callback) {
            delayed_decision->emplace(std::move(callback));
          },
          &delayed_decision),
      std::move(request));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(delayed_decision);

  loader_.reset();
  task_environment_.RunUntilIdle();
  std::move(*delayed_decision).Run(AdBlockDecision());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, terminal_factory_.NumPending());
}

}  // namespace
}  // namespace seoul::adblock
