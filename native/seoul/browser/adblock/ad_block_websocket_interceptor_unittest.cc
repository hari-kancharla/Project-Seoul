// Project Seoul WebSocket blocker interceptor tests.

#include "seoul/browser/adblock/ad_block_websocket_interceptor.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "content/public/test/browser_task_environment.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

class RecordingHandshakeClient
    : public network::mojom::WebSocketHandshakeClient {
 public:
  RecordingHandshakeClient() : receiver_(this) {}
  ~RecordingHandshakeClient() override = default;

  mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
  BindNewPipeAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  base::test::TestFuture<std::string, int32_t, int32_t>& failure() {
    return failure_;
  }

  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr request) override {
    opening_handshake_started_ = true;
  }

  void OnFailure(const std::string& message,
                 int32_t net_error,
                 int32_t response_code) override {
    failure_.SetValue(message, net_error, response_code);
  }

  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> socket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr response,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override {
    connection_established_ = true;
  }

 private:
  mojo::Receiver<network::mojom::WebSocketHandshakeClient> receiver_;
  base::test::TestFuture<std::string, int32_t, int32_t> failure_;
  bool opening_handshake_started_ = false;
  bool connection_established_ = false;
};

class AdBlockWebSocketInterceptorTest : public testing::Test {
 protected:
  static AdBlockRequest WebSocketRequest(std::string url) {
    AdBlockRequest request;
    request.url = std::move(url);
    request.request_type = "websocket";
    request.factory_type = AdBlockFactoryType::kWebSocket;
    return request;
  }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(AdBlockWebSocketInterceptorTest,
       AllowsHandshakeAndReturnsOriginalClient) {
  RecordingHandshakeClient client;
  bool continued = false;
  std::optional<mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>>
      continued_client;
  int checks = 0;

  AdBlockWebSocketInterceptor::InterceptForTesting(
      base::BindRepeating(
          [](int* checks, AdBlockRequest request,
             AdBlockWebSocketInterceptor::DecisionCallback callback) {
            ++*checks;
            EXPECT_EQ("wss://socket.example/live", request.url);
            EXPECT_EQ("websocket", request.request_type);
            std::move(callback).Run(AdBlockDecision());
          },
          &checks),
      WebSocketRequest("wss://socket.example/live"),
      client.BindNewPipeAndPassRemote(),
      base::BindOnce(
          [](bool* continued,
             std::optional<mojo::PendingRemote<
                 network::mojom::WebSocketHandshakeClient>>* continued_client,
             mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
                 client) {
            *continued = true;
            continued_client->emplace(std::move(client));
          },
          &continued, &continued_client));

  EXPECT_TRUE(continued);
  EXPECT_TRUE(continued_client);
  EXPECT_TRUE(continued_client->is_valid());
  EXPECT_EQ(1, checks);
}

TEST_F(AdBlockWebSocketInterceptorTest,
       BlocksHandshakeBeforeNetworkFactoryRuns) {
  RecordingHandshakeClient client;
  bool continued = false;

  AdBlockWebSocketInterceptor::InterceptForTesting(
      base::BindRepeating(
          [](AdBlockRequest,
             AdBlockWebSocketInterceptor::DecisionCallback callback) {
            AdBlockDecision decision;
            decision.action = AdBlockAction::kBlock;
            decision.matched = true;
            std::move(callback).Run(std::move(decision));
          }),
      WebSocketRequest("ws://ads.example/socket"),
      client.BindNewPipeAndPassRemote(),
      base::BindOnce(
          [](bool* continued,
             mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>) {
            *continued = true;
          },
          &continued));

  ASSERT_TRUE(client.failure().Wait());
  EXPECT_EQ("Blocked by Seoul", client.failure().Get<0>());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client.failure().Get<1>());
  EXPECT_EQ(-1, client.failure().Get<2>());
  EXPECT_FALSE(continued);
}

TEST_F(AdBlockWebSocketInterceptorTest,
       RedirectOrRewriteDecisionFailsClosed) {
  for (const AdBlockAction action :
       {AdBlockAction::kRedirect, AdBlockAction::kRewrite}) {
    RecordingHandshakeClient client;
    bool continued = false;
    AdBlockWebSocketInterceptor::InterceptForTesting(
        base::BindRepeating(
            [](AdBlockAction action, AdBlockRequest,
               AdBlockWebSocketInterceptor::DecisionCallback callback) {
              AdBlockDecision decision;
              decision.action = action;
              std::move(callback).Run(std::move(decision));
            },
            action),
        WebSocketRequest("wss://ads.example/socket"),
        client.BindNewPipeAndPassRemote(),
        base::BindOnce(
            [](bool* continued,
               mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>) {
              *continued = true;
            },
            &continued));

    ASSERT_TRUE(client.failure().Wait());
    EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client.failure().Get<1>());
    EXPECT_FALSE(continued);
  }
}

TEST_F(AdBlockWebSocketInterceptorTest,
       AsyncAllowOwnsHandshakeUntilDecisionCompletes) {
  RecordingHandshakeClient client;
  std::optional<AdBlockWebSocketInterceptor::DecisionCallback> delayed_decision;
  bool continued = false;

  AdBlockWebSocketInterceptor::InterceptForTesting(
      base::BindRepeating(
          [](std::optional<AdBlockWebSocketInterceptor::DecisionCallback>*
                 delayed_decision,
             AdBlockRequest,
             AdBlockWebSocketInterceptor::DecisionCallback callback) {
            delayed_decision->emplace(std::move(callback));
          },
          &delayed_decision),
      WebSocketRequest("wss://socket.example/async"),
      client.BindNewPipeAndPassRemote(),
      base::BindOnce(
          [](bool* continued,
             mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>) {
            *continued = true;
          },
          &continued));

  EXPECT_TRUE(delayed_decision);
  EXPECT_FALSE(continued);
  std::move(*delayed_decision).Run(AdBlockDecision());
  EXPECT_TRUE(continued);
}

TEST_F(AdBlockWebSocketInterceptorTest,
       UnsupportedSchemeBypassesEngineAndContinues) {
  RecordingHandshakeClient client;
  bool continued = false;
  int checks = 0;

  AdBlockWebSocketInterceptor::InterceptForTesting(
      base::BindRepeating(
          [](int* checks, AdBlockRequest,
             AdBlockWebSocketInterceptor::DecisionCallback callback) {
            ++*checks;
            std::move(callback).Run(AdBlockDecision());
          },
          &checks),
      WebSocketRequest("chrome://settings"), client.BindNewPipeAndPassRemote(),
      base::BindOnce(
          [](bool* continued,
             mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>) {
            *continued = true;
          },
          &continued));

  EXPECT_TRUE(continued);
  EXPECT_EQ(0, checks);
}

}  // namespace
}  // namespace seoul::adblock
