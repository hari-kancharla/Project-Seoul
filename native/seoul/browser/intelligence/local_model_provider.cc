// Project Seoul hybrid intelligence.

#include "seoul/browser/intelligence/local_model_provider.h"

#include <utility>

#include "base/functional/bind.h"
#include "seoul/browser/intelligence/provider_protocol.h"

namespace seoul {

LocalModelConfig::LocalModelConfig() = default;
LocalModelConfig::LocalModelConfig(const LocalModelConfig&) = default;
LocalModelConfig::LocalModelConfig(LocalModelConfig&&) = default;
LocalModelConfig& LocalModelConfig::operator=(const LocalModelConfig&) =
    default;
LocalModelConfig& LocalModelConfig::operator=(LocalModelConfig&&) = default;
LocalModelConfig::~LocalModelConfig() = default;

LocalModelProvider::LocalModelProvider(LocalModelConfig config,
                                       HttpTransport* transport)
    : config_(std::move(config)), transport_(transport) {
  config_.capabilities.locality = ModelLocality::kLocal;
}

LocalModelProvider::~LocalModelProvider() { Cancel(); }

std::string LocalModelProvider::provider_id() const {
  return "local:" + config_.model_id;
}

ModelCapabilities LocalModelProvider::capabilities() const {
  return config_.capabilities;
}

void LocalModelProvider::Generate(const GenerationRequest& request,
                                  GenerateCallback callback) {
  if (pending_) {
    std::move(callback).Run(base::unexpected(
        "The model is answering another request. Try again when it finishes."));
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  // Hard local-only guard: refuse before any bytes leave if the endpoint is
  // not loopback. This is what makes "local" trustworthy.
  if (!IsLocalOnlyEndpoint(config_.endpoint_url)) {
    std::move(callback).Run(
        base::unexpected("local provider endpoint is not a loopback address"));
    return;
  }
  if (!transport_) {
    std::move(callback).Run(base::unexpected("no transport configured"));
    return;
  }

  pending_ = std::move(callback);
  accumulator_ = std::make_unique<StreamingAccumulator>(
      base::BindRepeating(&chat_completions::ParseStreamPayload));

  HttpRequest http;
  http.method = "POST";
  http.url = config_.endpoint_url;
  http.expect_event_stream = true;
  http.headers.push_back({"Content-Type", "application/json"});
  http.body = chat_completions::BuildRequestBody(config_.model_id, request,
                                                 /*stream=*/true);

  HttpStreamCallbacks callbacks;
  callbacks.on_chunk = base::BindRepeating(
      [](base::WeakPtr<LocalModelProvider> self, std::string_view chunk) {
        if (self && self->pending_) self->accumulator_->Feed(chunk);
      },
      weak_factory_.GetWeakPtr());
  callbacks.on_complete = base::BindOnce(
      [](base::WeakPtr<LocalModelProvider> self, int http_status,
         const std::string& transport_error) {
        if (!self || !self->pending_) return;
        self->active_handle_ = 0;
        if (!transport_error.empty()) {
          std::move(self->pending_).Run(base::unexpected(transport_error));
          return;
        }
        if (http_status < 200 || http_status >= 300) {
          std::move(self->pending_)
              .Run(base::unexpected("local endpoint returned HTTP " +
                                    std::to_string(http_status)));
          return;
        }
        if (self->accumulator_->has_error()) {
          std::move(self->pending_)
              .Run(base::unexpected(self->accumulator_->error()));
          return;
        }
        GenerationResult result = self->accumulator_->Finish();
        if (self->accumulator_->has_error() || result.truncated ||
            result.text.empty()) {
          std::move(self->pending_).Run(base::unexpected(
              self->accumulator_->has_error() ? self->accumulator_->error() :
              "The local model response was interrupted or empty. Try again."));
          return;
        }
        std::move(self->pending_).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr());

  const auto self = weak_factory_.GetWeakPtr();
  const int handle = transport_->Start(http, std::move(callbacks));
  if (self && self->pending_) self->active_handle_ = handle;
}

void LocalModelProvider::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  pending_.Reset();
  if (transport_ && active_handle_ != 0) {
    transport_->Cancel(active_handle_);
  }
  active_handle_ = 0;
  accumulator_.reset();
}

int64_t LocalModelProvider::EstimateCostMicrodollars(int, int) const {
  return 0;  // local inference has no per-token cost
}

}  // namespace seoul
