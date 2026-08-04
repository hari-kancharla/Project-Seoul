// Project Seoul browser-process WebSocket blocker interceptor.

#include "seoul/browser/adblock/ad_block_websocket_interceptor.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "services/network/public/mojom/websocket.mojom.h"

namespace seoul::adblock {
namespace {

constexpr char kBlockedWebSocketMessage[] = "Blocked by Seoul";

void CheckWithService(base::WeakPtr<AdBlockService> service,
                      AdBlockRequest request,
                      AdBlockWebSocketInterceptor::DecisionCallback callback) {
  if (!service) {
    std::move(callback).Run(AdBlockDecision());
    return;
  }
  service->CheckRequest(std::move(request), std::move(callback));
}

class PendingWebSocketCheck {
 public:
  PendingWebSocketCheck(
      AdBlockWebSocketInterceptor::CheckRequestCallback check_request,
      AdBlockRequest request,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      AdBlockWebSocketInterceptor::ContinueCallback continue_callback)
      : check_request_(std::move(check_request)),
        request_(std::move(request)),
        handshake_client_(std::move(handshake_client)),
        continue_callback_(std::move(continue_callback)) {}

  PendingWebSocketCheck(const PendingWebSocketCheck&) = delete;
  PendingWebSocketCheck& operator=(const PendingWebSocketCheck&) = delete;

  static void Start(std::unique_ptr<PendingWebSocketCheck> check) {
    CHECK(check->check_request_);
    CHECK(check->continue_callback_);

    if (!GURL(check->request_.url).SchemeIsWSOrWSS()) {
      check->Continue();
      return;
    }

    AdBlockWebSocketInterceptor::CheckRequestCallback check_request =
        check->check_request_;
    AdBlockRequest request = std::move(check->request_);
    check_request.Run(
        std::move(request),
        base::BindOnce(&PendingWebSocketCheck::OnDecision, std::move(check)));
  }

 private:
  static void OnDecision(std::unique_ptr<PendingWebSocketCheck> check,
                         AdBlockDecision decision) {
    if (decision.action == AdBlockAction::kAllow) {
      check->Continue();
      return;
    }

    mojo::Remote<network::mojom::WebSocketHandshakeClient> client(
        std::move(check->handshake_client_));
    if (client.is_bound()) {
      client->OnFailure(kBlockedWebSocketMessage, net::ERR_BLOCKED_BY_CLIENT,
                        /*response_code=*/-1);
    }
  }

  void Continue() {
    std::move(continue_callback_).Run(std::move(handshake_client_));
  }

  AdBlockWebSocketInterceptor::CheckRequestCallback check_request_;
  AdBlockRequest request_;
  mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
      handshake_client_;
  AdBlockWebSocketInterceptor::ContinueCallback continue_callback_;
};

}  // namespace

// static
bool AdBlockWebSocketInterceptor::ShouldIntercept(
    content::RenderFrameHost* frame) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!frame) {
    return false;
  }
  Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
  return profile && AdBlockServiceFactory::GetForProfile(profile);
}

// static
void AdBlockWebSocketInterceptor::MaybeIntercept(
    content::RenderFrameHost* frame,
    const GURL& url,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    ContinueCallback continue_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(frame);

  Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
  AdBlockService* service =
      profile ? AdBlockServiceFactory::GetForProfile(profile) : nullptr;

  const url::Origin initiator_origin = frame->GetLastCommittedOrigin();
  content::RenderFrameHost* outermost_main_frame =
      frame->GetOutermostMainFrame();
  std::optional<url::Origin> top_frame_origin;
  if (outermost_main_frame) {
    top_frame_origin = outermost_main_frame->GetLastCommittedOrigin();
  }

  AdBlockRequest request = BuildWebSocketAdBlockRequest(
      url, std::move(top_frame_origin), initiator_origin,
      frame->GetGlobalFrameToken());
  CheckRequestCallback check_request = base::BindRepeating(
      &CheckWithService,
      service ? service->GetWeakPtr() : base::WeakPtr<AdBlockService>());
  InterceptForTesting(std::move(check_request), std::move(request),
                      std::move(handshake_client),
                      std::move(continue_callback));
}

// static
void AdBlockWebSocketInterceptor::InterceptForTesting(
    CheckRequestCallback check_request,
    AdBlockRequest request,
    mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
        handshake_client,
    ContinueCallback continue_callback) {
  PendingWebSocketCheck::Start(std::make_unique<PendingWebSocketCheck>(
      std::move(check_request), std::move(request), std::move(handshake_client),
      std::move(continue_callback)));
}

}  // namespace seoul::adblock
