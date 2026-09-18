// Project Seoul hybrid intelligence.

#include "seoul/browser/intelligence/cloud_model_provider.h"

#include <utility>

#include "base/functional/bind.h"
#include "seoul/browser/intelligence/provider_protocol.h"
#include "url/gurl.h"

namespace seoul {

CloudModelConfig::CloudModelConfig() = default;
CloudModelConfig::CloudModelConfig(const CloudModelConfig&) = default;
CloudModelConfig::CloudModelConfig(CloudModelConfig&&) = default;
CloudModelConfig& CloudModelConfig::operator=(const CloudModelConfig&) =
    default;
CloudModelConfig& CloudModelConfig::operator=(CloudModelConfig&&) = default;
CloudModelConfig::~CloudModelConfig() = default;

CloudModelProvider::CloudModelProvider(CloudModelConfig config,
                                       HttpTransport* transport,
                                       CredentialStore* credentials)
    : config_(std::move(config)),
      transport_(transport),
      credentials_(credentials) {
  config_.capabilities.locality = ModelLocality::kCloud;
}

CloudModelProvider::~CloudModelProvider() { Cancel(); }

std::string CloudModelProvider::provider_id() const {
  return "cloud:" + config_.model_id;
}

ModelCapabilities CloudModelProvider::capabilities() const {
  return config_.capabilities;
}

void CloudModelProvider::Generate(const GenerationRequest& request,
                                  GenerateCallback callback) {
  if (pending_) {
    std::move(callback).Run(base::unexpected(
        "The model is answering another request. Try again when it finishes."));
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  const GURL endpoint(config_.endpoint_url);
  if (!endpoint.is_valid() || !endpoint.SchemeIs("https") ||
      endpoint.has_username() || endpoint.has_password() || endpoint.has_ref()) {
    std::move(callback).Run(base::unexpected("Invalid secure model endpoint."));
    return;
  }
  if (!transport_ || !credentials_) {
    std::move(callback).Run(base::unexpected("provider not configured"));
    return;
  }
  // Fetch the key at the last moment and hold it only for this call. It is put
  // into the request header and never stored on the provider or logged.
  std::optional<std::string> key = credentials_->Get(config_.credential_account);
  if (!key || key->empty()) {
    std::move(callback).Run(
        base::unexpected("no API key configured for this provider"));
    return;
  }

  pending_ = std::move(callback);
  accumulator_ = std::make_unique<StreamingAccumulator>(
      base::BindRepeating(&messages::ParseStreamPayload));

  HttpRequest http;
  http.method = "POST";
  http.url = config_.endpoint_url;
  http.expect_event_stream = true;
  http.headers.push_back({"Content-Type", "application/json"});
  http.headers.push_back({"Authorization", "Bearer " + *key});
  if (!config_.api_version_header.empty()) {
    http.headers.push_back({"anthropic-version", config_.api_version_header});
  }
  http.body =
      messages::BuildRequestBody(config_.model_id, request, /*stream=*/true);
  // The local `key` string drops out of scope here; it is not retained.

  HttpStreamCallbacks callbacks;
  callbacks.on_chunk = base::BindRepeating(
      [](base::WeakPtr<CloudModelProvider> self, std::string_view chunk) {
        if (self && self->pending_) self->accumulator_->Feed(chunk);
      },
      weak_factory_.GetWeakPtr());
  callbacks.on_complete = base::BindOnce(
      [](base::WeakPtr<CloudModelProvider> self, int http_status,
         const std::string& transport_error) {
        if (!self || !self->pending_) return;
        self->active_handle_ = 0;
        if (!transport_error.empty()) {
          std::move(self->pending_).Run(base::unexpected(transport_error));
          return;
        }
        if (http_status < 200 || http_status >= 300) {
          std::move(self->pending_)
              .Run(base::unexpected(
                  messages::MapErrorResponse(http_status, std::string())));
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
              "The model response was interrupted or empty. Try again."));
          return;
        }
        result.usage.cost_microdollars = self->EstimateCostMicrodollars(
            result.usage.input_tokens, result.usage.output_tokens);
        std::move(self->pending_).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr());

  const auto self = weak_factory_.GetWeakPtr();
  const int handle = transport_->Start(http, std::move(callbacks));
  if (self && self->pending_) self->active_handle_ = handle;
}

void CloudModelProvider::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  pending_.Reset();
  if (transport_ && active_handle_ != 0) {
    transport_->Cancel(active_handle_);
  }
  active_handle_ = 0;
  accumulator_.reset();
}

int64_t CloudModelProvider::EstimateCostMicrodollars(int input_tokens,
                                                     int output_tokens) const {
  const int64_t input_cost =
      static_cast<int64_t>(input_tokens) *
      config_.capabilities.input_cost_per_mtok_microdollars / 1000000;
  const int64_t output_cost =
      static_cast<int64_t>(output_tokens) *
      config_.capabilities.output_cost_per_mtok_microdollars / 1000000;
  return input_cost + output_cost;
}

}  // namespace seoul
