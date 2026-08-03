// Project Seoul browser-process URLLoaderFactory blocker interceptor.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_INTERCEPTOR_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_INTERCEPTOR_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_routing_id.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "seoul/browser/adblock/ad_block_decision.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content

namespace seoul::adblock {

class AdBlockRequestInterceptor : public network::mojom::URLLoaderFactory {
 public:
  using DecisionCallback = base::OnceCallback<void(AdBlockDecision)>;
  using CheckRequestCallback =
      base::RepeatingCallback<void(AdBlockRequest, DecisionCallback)>;
  using DisconnectCallback =
      base::OnceCallback<void(AdBlockRequestInterceptor*)>;

  // Adds a proxy only for eligible regular-profile factory types.
  static void MaybeProxyRequest(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      content::ContentBrowserClient::URLLoaderFactoryType type,
      const net::IsolationInfo& isolation_info,
      network::URLLoaderFactoryBuilder& factory_builder);

  static bool IsEligibleFactoryType(
      content::ContentBrowserClient::URLLoaderFactoryType type);

  // Public for deterministic URLLoaderFactory unit tests. Production instances
  // are owned by AdBlockService.
  AdBlockRequestInterceptor(
      CheckRequestCallback check_request,
      std::optional<url::Origin> top_frame_origin,
      std::optional<content::GlobalRenderFrameHostToken> render_frame_token,
      AdBlockFactoryType factory_type,
      network::URLLoaderFactoryBuilder& factory_builder,
      DisconnectCallback disconnect_callback);
  ~AdBlockRequestInterceptor() override;

  AdBlockRequestInterceptor(const AdBlockRequestInterceptor&) = delete;
  AdBlockRequestInterceptor& operator=(const AdBlockRequestInterceptor&) =
      delete;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory>
                 loader_receiver) override;

 private:
  class InProgressRequest;

  friend class InProgressRequest;

  void CheckRequest(const network::ResourceRequest& request,
                    DecisionCallback callback);
  void RemoveRequest(uint64_t request_id);
  void OnTargetFactoryDisconnected();
  void OnProxyReceiverDisconnected();
  void MaybeDisconnect();

  CheckRequestCallback check_request_;
  const std::optional<url::Origin> top_frame_origin_;
  const std::optional<content::GlobalRenderFrameHostToken> render_frame_token_;
  const AdBlockFactoryType factory_type_;

  mojo::ReceiverSet<network::mojom::URLLoaderFactory> proxy_receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  std::map<uint64_t, std::unique_ptr<InProgressRequest>> requests_;
  uint64_t next_request_id_ = 1;
  DisconnectCallback disconnect_callback_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_REQUEST_INTERCEPTOR_H_
