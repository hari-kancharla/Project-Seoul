// Project Seoul browser-process URLLoaderFactory blocker interceptor.

#include "seoul/browser/adblock/ad_block_request_interceptor.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/base/big_buffer.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/string_data_source.h"
#include "net/base/data_url.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "seoul/browser/adblock/ad_block_resource_catalog.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace seoul::adblock {
namespace {

std::optional<AdBlockFactoryType> ToAdBlockFactoryType(
    content::ContentBrowserClient::URLLoaderFactoryType type) {
  using FactoryType = content::ContentBrowserClient::URLLoaderFactoryType;
  switch (type) {
    case FactoryType::kDocumentSubResource:
      return AdBlockFactoryType::kDocumentSubresource;
    case FactoryType::kWorkerMainResource:
      return AdBlockFactoryType::kWorkerMainResource;
    case FactoryType::kWorkerSubResource:
      return AdBlockFactoryType::kWorkerSubresource;
    case FactoryType::kServiceWorkerScript:
      return AdBlockFactoryType::kServiceWorkerScript;
    case FactoryType::kServiceWorkerSubResource:
      return AdBlockFactoryType::kServiceWorkerSubresource;
    case FactoryType::kNavigation:
    case FactoryType::kDownload:
    case FactoryType::kPrefetch:
    case FactoryType::kDevTools:
    case FactoryType::kEarlyHints:
      return std::nullopt;
  }
}

void CheckWithService(base::WeakPtr<AdBlockService> service,
                      AdBlockRequest request,
                      AdBlockRequestInterceptor::DecisionCallback callback) {
  if (!service) {
    std::move(callback).Run(AdBlockDecision());
    return;
  }
  service->CheckRequest(std::move(request), std::move(callback));
}

}  // namespace

class AdBlockRequestInterceptor::InProgressRequest
    : public network::mojom::URLLoader,
      public network::mojom::URLLoaderClient {
 public:
  InProgressRequest(
      AdBlockRequestInterceptor* factory,
      uint64_t request_id,
      int32_t network_request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client)
      : factory_(factory),
        request_id_(request_id),
        network_request_id_(network_request_id),
        options_(options),
        request_(request),
        traffic_annotation_(traffic_annotation),
        proxied_loader_receiver_(this, std::move(loader_receiver)),
        target_client_(std::move(client)),
        proxied_client_receiver_(this) {
    target_client_.set_disconnect_handler(base::BindOnce(
        &InProgressRequest::OnClientDisconnected, weak_factory_.GetWeakPtr()));
    proxied_loader_receiver_.set_disconnect_handler(base::BindOnce(
        &InProgressRequest::OnClientDisconnected, weak_factory_.GetWeakPtr()));
  }

  ~InProgressRequest() override = default;

  InProgressRequest(const InProgressRequest&) = delete;
  InProgressRequest& operator=(const InProgressRequest&) = delete;

  void Start() {
    CheckCandidate(request_,
                   base::BindOnce(&InProgressRequest::OnInitialDecision,
                                  weak_factory_.GetWeakPtr()));
  }

  // network::mojom::URLLoader:
  void FollowRedirect(
      const std::vector<std::string>& removed_headers,
      const net::HttpRequestHeaders& modified_headers,
      const net::HttpRequestHeaders& modified_cors_exempt_headers,
      const std::optional<GURL>& new_url) override {
    if (completed_ || !target_loader_.is_bound()) {
      return;
    }

    FollowRedirectParams params;
    params.removed_headers = removed_headers;
    params.modified_headers = modified_headers;
    params.modified_cors_exempt_headers = modified_cors_exempt_headers;
    params.new_url = new_url;

    if (new_url && *new_url != request_.url) {
      network::ResourceRequest candidate(request_);
      candidate.url = *new_url;
      network::ResourceRequest callback_candidate(candidate);
      CheckCandidate(
          candidate,
          base::BindOnce(&InProgressRequest::OnFollowRedirectDecision,
                         weak_factory_.GetWeakPtr(), std::move(params),
                         std::move(callback_candidate)));
      return;
    }
    ForwardFollowRedirect(std::move(params));
  }

  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override {
    if (target_loader_.is_bound()) {
      target_loader_->SetPriority(priority, intra_priority_value);
      return;
    }
    pending_priority_ = std::make_pair(priority, intra_priority_value);
  }

  // network::mojom::URLLoaderClient:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    if (target_client_.is_bound()) {
      target_client_->OnReceiveEarlyHints(std::move(early_hints));
    }
  }

  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr response_head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override {
    if (target_client_.is_bound()) {
      target_client_->OnReceiveResponse(std::move(response_head),
                                        std::move(body),
                                        std::move(cached_metadata));
    }
  }

  void OnReceiveRedirect(
      const net::RedirectInfo& redirect_info,
      network::mojom::URLResponseHeadPtr response_head) override {
    if (completed_) {
      return;
    }
    network::ResourceRequest candidate(request_);
    candidate.UpdateOnRedirect(redirect_info);
    network::ResourceRequest callback_candidate(candidate);
    CheckCandidate(candidate,
                   base::BindOnce(&InProgressRequest::OnRedirectDecision,
                                  weak_factory_.GetWeakPtr(), redirect_info,
                                  std::move(response_head),
                                  std::move(callback_candidate)));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    if (target_client_.is_bound()) {
      target_client_->OnUploadProgress(current_position, total_size,
                                       std::move(callback));
    } else {
      std::move(callback).Run();
    }
  }

  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    if (target_client_.is_bound()) {
      target_client_->OnTransferSizeUpdated(transfer_size_diff);
    }
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    if (completed_) {
      return;
    }
    completed_ = true;
    if (target_client_.is_bound()) {
      target_client_->OnComplete(status);
    }
    factory_->RemoveRequest(request_id_);
  }

 private:
  struct FollowRedirectParams {
    FollowRedirectParams() = default;
    FollowRedirectParams(const FollowRedirectParams&) = delete;
    FollowRedirectParams& operator=(const FollowRedirectParams&) = delete;
    FollowRedirectParams(FollowRedirectParams&&) = default;
    FollowRedirectParams& operator=(FollowRedirectParams&&) = default;
    ~FollowRedirectParams() = default;

    std::vector<std::string> removed_headers;
    net::HttpRequestHeaders modified_headers;
    net::HttpRequestHeaders modified_cors_exempt_headers;
    std::optional<GURL> new_url;
  };

  void CheckCandidate(const network::ResourceRequest& candidate,
                      DecisionCallback callback) {
    // Navigation factories are excluded before this interceptor is created.
    // Do not trust a request-carried frame flag to bypass an eligible
    // document/worker factory.
    if (!IsSupportedRequestScheme(candidate.url)) {
      std::move(callback).Run(AdBlockDecision());
      return;
    }
    factory_->CheckRequest(candidate, std::move(callback));
  }

  void OnInitialDecision(AdBlockDecision decision) {
    if (ShouldBlock(decision)) {
      Block();
      return;
    }
    if (decision.action == AdBlockAction::kRedirect) {
      RespondWithReplacement(decision);
      return;
    }
    ApplyRewrite(decision, &request_);
    if (!factory_->target_factory_.is_bound()) {
      CompleteWithError(net::ERR_FAILED);
      return;
    }

    factory_->target_factory_->CreateLoaderAndStart(
        target_loader_.BindNewPipeAndPassReceiver(), network_request_id_,
        options_, request_, proxied_client_receiver_.BindNewPipeAndPassRemote(),
        traffic_annotation_);
    proxied_client_receiver_.set_disconnect_handler(
        base::BindOnce(&InProgressRequest::OnTargetLoaderDisconnected,
                       weak_factory_.GetWeakPtr()));
    if (pending_priority_) {
      target_loader_->SetPriority(pending_priority_->first,
                                  pending_priority_->second);
      pending_priority_.reset();
    }
  }

  void OnRedirectDecision(net::RedirectInfo redirect_info,
                          network::mojom::URLResponseHeadPtr response_head,
                          network::ResourceRequest candidate,
                          AdBlockDecision decision) {
    if (ShouldBlock(decision)) {
      Block();
      return;
    }
    if (decision.action == AdBlockAction::kRedirect) {
      request_ = std::move(candidate);
      RespondWithReplacement(decision);
      return;
    }
    if (ApplyRewrite(decision, &candidate)) {
      redirect_info.new_url = candidate.url;
      pending_follow_redirect_url_override_ = candidate.url;
    }
    request_ = std::move(candidate);
    if (target_client_.is_bound()) {
      target_client_->OnReceiveRedirect(redirect_info,
                                        std::move(response_head));
    }
  }

  void OnFollowRedirectDecision(FollowRedirectParams params,
                                network::ResourceRequest candidate,
                                AdBlockDecision decision) {
    if (ShouldBlock(decision)) {
      Block();
      return;
    }
    if (decision.action == AdBlockAction::kRedirect) {
      request_ = std::move(candidate);
      RespondWithReplacement(decision);
      return;
    }
    if (ApplyRewrite(decision, &candidate)) {
      params.new_url = candidate.url;
    }
    request_ = std::move(candidate);
    ForwardFollowRedirect(std::move(params));
  }

  bool ShouldBlock(const AdBlockDecision& decision) const {
    return decision.action == AdBlockAction::kBlock;
  }

  bool ApplyRewrite(const AdBlockDecision& decision,
                    network::ResourceRequest* candidate) {
    if (decision.action != AdBlockAction::kRewrite || !decision.rewritten_url) {
      return false;
    }
    const GURL rewritten_url(*decision.rewritten_url);
    if (!IsSafeAdBlockUrlRewrite(candidate->url, rewritten_url,
                                 candidate->method)) {
      return false;
    }
    candidate->url = rewritten_url;
    return true;
  }

  void ForwardFollowRedirect(FollowRedirectParams params) {
    if (!target_loader_.is_bound()) {
      CompleteWithError(net::ERR_FAILED);
      return;
    }
    if (!params.new_url && pending_follow_redirect_url_override_) {
      params.new_url = std::move(pending_follow_redirect_url_override_);
    }
    pending_follow_redirect_url_override_.reset();
    target_loader_->FollowRedirect(
        params.removed_headers, params.modified_headers,
        params.modified_cors_exempt_headers, params.new_url);
  }

  void Block() {
    target_loader_.reset();
    proxied_client_receiver_.reset();
    CompleteWithError(net::ERR_BLOCKED_BY_CLIENT);
  }

  void RespondWithReplacement(const AdBlockDecision& decision) {
    if (!decision.redirect) {
      Block();
      return;
    }
    const std::optional<AdBlockResource> resource =
        FindAdBlockResourceByDataUrl(*decision.redirect);
    if (!resource) {
      Block();
      return;
    }

    target_loader_.reset();
    proxied_client_receiver_.reset();

    synthetic_body_.clear();
    auto response = network::mojom::URLResponseHead::New();
    const net::Error parse_result = net::DataURL::BuildResponse(
        GURL(resource->data_url), request_.method, &response->mime_type,
        &response->charset, &synthetic_body_, &response->headers);
    if (parse_result != net::OK) {
      CompleteWithError(parse_result);
      return;
    }

    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    if (mojo::CreateDataPipe(nullptr, producer, consumer) != MOJO_RESULT_OK) {
      CompleteWithError(net::ERR_INSUFFICIENT_RESOURCES);
      return;
    }
    if (!target_client_.is_bound()) {
      OnClientDisconnected();
      return;
    }
    target_client_->OnReceiveResponse(std::move(response), std::move(consumer),
                                      std::nullopt);

    synthetic_producer_ =
        std::make_unique<mojo::DataPipeProducer>(std::move(producer));
    synthetic_producer_->Write(
        std::make_unique<mojo::StringDataSource>(
            synthetic_body_, mojo::StringDataSource::AsyncWritingMode::
                                 STRING_STAYS_VALID_UNTIL_COMPLETION),
        base::BindOnce(&InProgressRequest::OnSyntheticBodyWritten,
                       weak_factory_.GetWeakPtr()));
  }

  void OnSyntheticBodyWritten(MojoResult result) {
    synthetic_producer_.reset();
    if (result != MOJO_RESULT_OK) {
      CompleteWithError(net::ERR_FAILED);
      return;
    }
    if (completed_) {
      return;
    }
    completed_ = true;
    network::URLLoaderCompletionStatus status(net::OK);
    status.encoded_data_length = synthetic_body_.size();
    status.encoded_body_length = synthetic_body_.size();
    status.decoded_body_length = synthetic_body_.size();
    if (target_client_.is_bound()) {
      target_client_->OnComplete(status);
    }
    factory_->RemoveRequest(request_id_);
  }

  void CompleteWithError(int error) {
    if (completed_) {
      return;
    }
    completed_ = true;
    if (target_client_.is_bound()) {
      target_client_->OnComplete(network::URLLoaderCompletionStatus(error));
    }
    factory_->RemoveRequest(request_id_);
  }

  void OnClientDisconnected() {
    if (completed_) {
      return;
    }
    completed_ = true;
    target_loader_.reset();
    proxied_client_receiver_.reset();
    synthetic_producer_.reset();
    factory_->RemoveRequest(request_id_);
  }

  void OnTargetLoaderDisconnected() { CompleteWithError(net::ERR_FAILED); }

  const raw_ptr<AdBlockRequestInterceptor> factory_;
  const uint64_t request_id_;
  const int32_t network_request_id_;
  const uint32_t options_;
  network::ResourceRequest request_;
  const net::MutableNetworkTrafficAnnotationTag traffic_annotation_;

  mojo::Receiver<network::mojom::URLLoader> proxied_loader_receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> target_client_;
  mojo::Receiver<network::mojom::URLLoaderClient> proxied_client_receiver_;
  mojo::Remote<network::mojom::URLLoader> target_loader_;
  std::optional<std::pair<net::RequestPriority, int32_t>> pending_priority_;
  std::optional<GURL> pending_follow_redirect_url_override_;
  std::string synthetic_body_;
  std::unique_ptr<mojo::DataPipeProducer> synthetic_producer_;
  bool completed_ = false;
  base::WeakPtrFactory<InProgressRequest> weak_factory_{this};
};

// static
void AdBlockRequestInterceptor::MaybeProxyRequest(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const std::optional<AdBlockFactoryType> factory_type =
      ToAdBlockFactoryType(type);
  if (!browser_context || !factory_type) {
    return;
  }

  Profile* profile = Profile::FromBrowserContext(browser_context);
  if (!profile) {
    return;
  }
  AdBlockService* service = AdBlockServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }

  std::optional<content::GlobalRenderFrameHostToken> frame_token;
  if (frame) {
    frame_token = frame->GetGlobalFrameToken();
  }
  service->AddRequestInterceptor(std::make_unique<AdBlockRequestInterceptor>(
      base::BindRepeating(&CheckWithService, service->GetWeakPtr()),
      isolation_info.top_frame_origin(), std::move(frame_token), *factory_type,
      factory_builder,
      base::BindOnce(&AdBlockService::RemoveRequestInterceptor,
                     service->GetWeakPtr())));
}

// static
bool AdBlockRequestInterceptor::IsEligibleFactoryType(
    content::ContentBrowserClient::URLLoaderFactoryType type) {
  return ToAdBlockFactoryType(type).has_value();
}

AdBlockRequestInterceptor::AdBlockRequestInterceptor(
    CheckRequestCallback check_request,
    std::optional<url::Origin> top_frame_origin,
    std::optional<content::GlobalRenderFrameHostToken> render_frame_token,
    AdBlockFactoryType factory_type,
    network::URLLoaderFactoryBuilder& factory_builder,
    DisconnectCallback disconnect_callback)
    : check_request_(std::move(check_request)),
      top_frame_origin_(std::move(top_frame_origin)),
      render_frame_token_(std::move(render_frame_token)),
      factory_type_(factory_type),
      disconnect_callback_(std::move(disconnect_callback)) {
  auto [receiver, target_factory] = factory_builder.Append();
  target_factory_.Bind(std::move(target_factory));
  target_factory_.set_disconnect_handler(
      base::BindOnce(&AdBlockRequestInterceptor::OnTargetFactoryDisconnected,
                     base::Unretained(this)));
  proxy_receivers_.Add(this, std::move(receiver));
  proxy_receivers_.set_disconnect_handler(base::BindRepeating(
      &AdBlockRequestInterceptor::OnProxyReceiverDisconnected,
      base::Unretained(this)));
}

AdBlockRequestInterceptor::~AdBlockRequestInterceptor() = default;

void AdBlockRequestInterceptor::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const uint64_t local_request_id = next_request_id_++;
  auto in_progress = std::make_unique<InProgressRequest>(
      this, local_request_id, request_id, options, request, traffic_annotation,
      std::move(loader_receiver), std::move(client));
  InProgressRequest* in_progress_ptr = in_progress.get();
  requests_.emplace(local_request_id, std::move(in_progress));
  in_progress_ptr->Start();
}

void AdBlockRequestInterceptor::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver) {
  proxy_receivers_.Add(this, std::move(loader_receiver));
}

void AdBlockRequestInterceptor::CheckRequest(
    const network::ResourceRequest& request,
    DecisionCallback callback) {
  check_request_.Run(BuildAdBlockRequest(request, top_frame_origin_,
                                         render_frame_token_, factory_type_),
                     std::move(callback));
}

void AdBlockRequestInterceptor::RemoveRequest(uint64_t request_id) {
  const auto it = requests_.find(request_id);
  CHECK(it != requests_.end());
  requests_.erase(it);
  MaybeDisconnect();
}

void AdBlockRequestInterceptor::OnTargetFactoryDisconnected() {
  target_factory_.reset();
  proxy_receivers_.Clear();
  MaybeDisconnect();
}

void AdBlockRequestInterceptor::OnProxyReceiverDisconnected() {
  if (proxy_receivers_.empty()) {
    target_factory_.reset();
  }
  MaybeDisconnect();
}

void AdBlockRequestInterceptor::MaybeDisconnect() {
  if (target_factory_.is_bound() || !requests_.empty() ||
      !disconnect_callback_) {
    return;
  }
  std::move(disconnect_callback_).Run(this);
}

}  // namespace seoul::adblock
