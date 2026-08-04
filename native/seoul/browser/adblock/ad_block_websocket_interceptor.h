// Project Seoul browser-process WebSocket blocker interceptor.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_WEBSOCKET_INTERCEPTOR_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_WEBSOCKET_INTERCEPTOR_H_

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "seoul/browser/adblock/ad_block_decision.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "services/network/public/mojom/websocket.mojom-forward.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace seoul::adblock {

class AdBlockWebSocketInterceptor {
 public:
  using DecisionCallback = base::OnceCallback<void(AdBlockDecision)>;
  using CheckRequestCallback =
      base::RepeatingCallback<void(AdBlockRequest, DecisionCallback)>;
  using ContinueCallback = base::OnceCallback<void(
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>)>;

  AdBlockWebSocketInterceptor() = delete;

  // Returns true when a frame belongs to a profile with Seoul blocking.
  static bool ShouldIntercept(content::RenderFrameHost* frame);

  // Asynchronously checks a frame-associated WS/WSS handshake. `continue`
  // receives the original handshake client only when the request is allowed.
  static void MaybeIntercept(
      content::RenderFrameHost* frame,
      const GURL& url,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      ContinueCallback continue_callback);

  // Deterministic boundary used by unit tests without a live Profile or frame.
  static void InterceptForTesting(
      CheckRequestCallback check_request,
      AdBlockRequest request,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      ContinueCallback continue_callback);
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_WEBSOCKET_INTERCEPTOR_H_
