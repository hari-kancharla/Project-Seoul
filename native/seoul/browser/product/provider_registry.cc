// Project Seoul product runtime: the provider registry.

#include "seoul/browser/product/provider_registry.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "seoul/browser/intelligence/cloud_model_provider.h"
#include "seoul/browser/intelligence/local_model_provider.h"
#include "seoul/browser/intelligence/provider_protocol.h"

namespace seoul {

namespace {

// A real JSON Schema (draft-style) describing the plan the reasoning provider
// must return. This is what a structured-output endpoint validates against;
// it mirrors what PlanFromValue accepts. Providers that enforce json_schema
// therefore return a plan-shaped object, and PlanFromValue + ValidatePlan
// still re-check it defensively before anything runs.
base::DictValue BuildPlanResponseSchema() {
  base::DictValue step_properties;
  step_properties.Set("id", base::DictValue().Set("type", "string"));
  base::ListValue kind_enum;
  kind_enum.Append("tool_call");
  kind_enum.Append("approval_gate");
  kind_enum.Append("user_input");
  step_properties.Set("kind", base::DictValue()
                                  .Set("type", "string")
                                  .Set("enum", std::move(kind_enum)));
  step_properties.Set("tool", base::DictValue().Set("type", "string"));
  step_properties.Set("args", base::DictValue().Set("type", "object"));
  step_properties.Set("prompt", base::DictValue().Set("type", "string"));

  base::ListValue step_required;
  step_required.Append("id");
  step_required.Append("kind");
  base::DictValue step_schema;
  step_schema.Set("type", "object");
  step_schema.Set("properties", std::move(step_properties));
  step_schema.Set("required", std::move(step_required));

  base::DictValue steps_schema;
  steps_schema.Set("type", "array");
  steps_schema.Set("items", std::move(step_schema));

  base::DictValue properties;
  properties.Set("goal", base::DictValue().Set("type", "string"));
  properties.Set("steps", std::move(steps_schema));

  base::ListValue required;
  required.Append("goal");
  required.Append("steps");

  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("properties", std::move(properties));
  schema.Set("required", std::move(required));
  return schema;
}

}  // namespace

ProviderStateSnapshot::ProviderStateSnapshot() = default;
ProviderStateSnapshot::ProviderStateSnapshot(const ProviderStateSnapshot&) =
    default;
ProviderStateSnapshot::ProviderStateSnapshot(ProviderStateSnapshot&&) = default;
ProviderStateSnapshot& ProviderStateSnapshot::operator=(
    const ProviderStateSnapshot&) = default;
ProviderStateSnapshot& ProviderStateSnapshot::operator=(
    ProviderStateSnapshot&&) = default;
ProviderStateSnapshot::~ProviderStateSnapshot() = default;

ProviderRegistry::ProviderRegistry(HttpTransport* local_transport,
                                   HttpTransport* cloud_transport,
                                   CredentialStore* credentials)
    : local_transport_(local_transport),
      cloud_transport_(cloud_transport),
      credentials_(credentials) {}

ProviderRegistry::~ProviderRegistry() = default;

bool ProviderRegistry::ConfigureLocal(const std::string& endpoint_url,
                                      const std::string& model_id) {
  // Local mode rejects deceptive non-loopback endpoints outright; there is no
  // override switch.
  if (!IsLocalOnlyEndpoint(endpoint_url) || model_id.empty()) {
    last_error_ = "Local endpoint must be a loopback address.";
    return false;
  }
  local_endpoint_ = endpoint_url;
  local_model_ = model_id;
  LocalModelConfig config;
  config.endpoint_url = endpoint_url;
  config.model_id = model_id;
  config.capabilities.text_generation = true;
  config.capabilities.structured_generation = true;
  config.capabilities.streaming = true;
  config.capabilities.locality = ModelLocality::kLocal;
  local_provider_ =
      std::make_unique<LocalModelProvider>(std::move(config), local_transport_);
  local_healthy_ = false;  // unknown until the next health check passes
  return true;
}

void ProviderRegistry::ClearLocal() {
  if (local_provider_) {
    local_provider_->Cancel();
  }
  local_provider_.reset();
  local_endpoint_.clear();
  local_model_.clear();
  local_healthy_ = false;
  local_models_discovered_.clear();
}

bool ProviderRegistry::ConfigureCloud(const std::string& model_id,
                                      bool enabled) {
  if (model_id.empty()) {
    return false;
  }
  cloud_model_ = model_id;
  cloud_enabled_ = enabled;
  CloudModelConfig config;
  config.model_id = model_id;
  config.credential_account = kCloudReasoningCredentialAccount;
  cloud_provider_ = std::make_unique<CloudModelProvider>(
      std::move(config), cloud_transport_, credentials_);
  return true;
}

void ProviderRegistry::SetCloudEnabled(bool enabled) {
  cloud_enabled_ = enabled;
}

void ProviderRegistry::ClearCloud() {
  if (cloud_provider_) {
    cloud_provider_->Cancel();
  }
  cloud_provider_.reset();
  cloud_model_.clear();
  cloud_enabled_ = false;
}

void ProviderRegistry::CheckLocalHealth(
    base::OnceCallback<void(bool)> callback) {
  if (local_endpoint_.empty() || !local_transport_) {
    local_healthy_ = false;
    std::move(callback).Run(false);
    return;
  }
  HttpRequest request;
  request.method = "GET";
  request.url = local_endpoint_ + "/models";
  auto body = std::make_unique<std::string>();
  std::string* body_ptr = body.get();
  HttpStreamCallbacks callbacks;
  callbacks.on_chunk = base::BindRepeating(
      [](std::string* accumulated, std::string_view chunk) {
        accumulated->append(chunk);
      },
      body_ptr);
  callbacks.on_complete = base::BindOnce(
      [](std::unique_ptr<std::string> owned_body,
         base::WeakPtr<ProviderRegistry> registry,
         base::OnceCallback<void(bool)> callback, int http_status,
         const std::string& transport_error) {
        if (registry) {
          registry->OnHealthResponse(std::move(callback),
                                     std::move(*owned_body), http_status,
                                     transport_error);
        }
      },
      std::move(body), weak_factory_.GetWeakPtr(), std::move(callback));
  local_transport_->Start(request, std::move(callbacks));
}

void ProviderRegistry::OnHealthResponse(base::OnceCallback<void(bool)> callback,
                                        std::string body,
                                        int http_status,
                                        const std::string& transport_error) {
  if (shutting_down_) {
    return;
  }
  local_healthy_ = http_status == 200 && transport_error.empty();
  local_models_discovered_.clear();
  if (local_healthy_) {
    // Chat-completions convention: {"data": [{"id": "<model>"}, ...]}.
    std::optional<base::Value> parsed = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
    if (parsed.has_value() && parsed->is_dict()) {
      if (const base::ListValue* data = parsed->GetDict().FindList("data")) {
        for (const base::Value& entry : *data) {
          const base::DictValue* dict = entry.GetIfDict();
          const std::string* id = dict ? dict->FindString("id") : nullptr;
          if (id && !id->empty() && local_models_discovered_.size() < 64) {
            local_models_discovered_.push_back(*id);
          }
        }
      }
    }
  } else {
    last_error_ = transport_error.empty() ? "Local endpoint returned an error."
                                          : transport_error;
  }
  std::move(callback).Run(local_healthy_);
}

ProviderStateSnapshot ProviderRegistry::Snapshot() const {
  ProviderStateSnapshot snapshot;
  snapshot.local_configured = static_cast<bool>(local_provider_);
  snapshot.local_healthy = local_healthy_;
  snapshot.local_endpoint = local_endpoint_;
  snapshot.local_model = local_model_;
  snapshot.local_models_discovered = local_models_discovered_;
  snapshot.cloud_configured =
      credentials_ &&
      credentials_->Get(kCloudReasoningCredentialAccount).has_value();
  snapshot.cloud_enabled = cloud_enabled_;
  snapshot.cloud_model = cloud_model_;
  snapshot.last_error = last_error_;
  return snapshot;
}

bool ProviderRegistry::cloud_available() const {
  return cloud_provider_ && cloud_enabled_ && credentials_ &&
         credentials_->Get(kCloudReasoningCredentialAccount).has_value();
}

bool ProviderRegistry::HasUsableProvider(bool allow_cloud_models) const {
  return local_available() || (allow_cloud_models && cloud_available());
}

ModelProvider* ProviderRegistry::PickProvider(
    bool prefer_local,
    bool allow_cloud_models) const {
  if (prefer_local) {
    if (local_available()) {
      return local_provider_.get();
    }
    return allow_cloud_models && cloud_available() ? cloud_provider_.get()
                                                   : nullptr;
  }
  if (allow_cloud_models && cloud_available()) {
    return cloud_provider_.get();
  }
  return local_available() ? local_provider_.get() : nullptr;
}

ModelPlanRequester ProviderRegistry::MakePlanRequester() {
  return base::BindRepeating(&ProviderRegistry::RequestPlan,
                             weak_factory_.GetWeakPtr());
}

void ProviderRegistry::RequestPlan(
    const std::string& prompt,
    bool prefer_local,
    bool allow_cloud_models,
    base::OnceCallback<void(std::optional<base::DictValue>, PlanOrigin)>
        callback) {
  ModelProvider* provider =
      PickProvider(prefer_local, allow_cloud_models);
  if (!provider || shutting_down_) {
    std::move(callback).Run(std::nullopt, PlanOrigin::kDeterministic);
    return;
  }
  const PlanOrigin origin = provider == local_provider_.get()
                                ? PlanOrigin::kLocalModel
                                : PlanOrigin::kCloudModel;
  GenerationRequest request;
  request.system_prompt =
      "You are a planner. Reply with exactly one JSON object matching the "
      "schema. No prose, no code.";
  request.user_prompt = prompt;
  request.response_schema = BuildPlanResponseSchema();
  request.max_output_tokens = 2048;
  provider->Generate(request, base::BindOnce(&ProviderRegistry::OnPlanGenerated,
                                             weak_factory_.GetWeakPtr(),
                                             std::move(callback), origin));
}

void ProviderRegistry::OnPlanGenerated(
    base::OnceCallback<void(std::optional<base::DictValue>, PlanOrigin)>
        callback,
    PlanOrigin origin,
    base::expected<GenerationResult, std::string> result) {
  if (shutting_down_) {
    return;
  }
  if (!result.has_value()) {
    last_error_ = result.error();
    std::move(callback).Run(std::nullopt, origin);
    return;
  }
  if (result->structured.is_dict()) {
    std::move(callback).Run(result->structured.GetDict().Clone(), origin);
    return;
  }
  // Some providers return the JSON as text; parse defensively.
  std::optional<base::Value> parsed = base::JSONReader::Read(result->text, base::JSON_PARSE_RFC);
  if (parsed.has_value() && parsed->is_dict()) {
    std::move(callback).Run(parsed->GetDict().Clone(), origin);
    return;
  }
  std::move(callback).Run(std::nullopt, origin);
}

void ProviderRegistry::Generate(const GenerationRequest& request,
                                bool prefer_local,
                                ModelProvider::GenerateCallback callback) {
  ModelProvider* provider =
      PickProvider(prefer_local, /*allow_cloud_models=*/true);
  if (!provider || shutting_down_) {
    std::move(callback).Run(
        base::unexpected("No reasoning provider is configured."));
    return;
  }
  provider->Generate(request, std::move(callback));
}

base::DictValue ProviderRegistry::TakePersistedState() const {
  base::DictValue state;
  state.Set("local_endpoint", local_endpoint_);
  state.Set("local_model", local_model_);
  state.Set("cloud_model", cloud_model_);
  state.Set("cloud_enabled", cloud_enabled_);
  return state;
}

void ProviderRegistry::RestorePersistedState(const base::DictValue& state) {
  const std::string* local_endpoint = state.FindString("local_endpoint");
  const std::string* local_model = state.FindString("local_model");
  if (local_endpoint && local_model && !local_endpoint->empty() &&
      !local_model->empty()) {
    ConfigureLocal(*local_endpoint, *local_model);
  }
  const std::string* cloud_model = state.FindString("cloud_model");
  if (cloud_model && !cloud_model->empty()) {
    ConfigureCloud(*cloud_model,
                   state.FindBool("cloud_enabled").value_or(false));
  }
}

void ProviderRegistry::Shutdown() {
  shutting_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  if (local_provider_) {
    local_provider_->Cancel();
  }
  if (cloud_provider_) {
    cloud_provider_->Cancel();
  }
}

}  // namespace seoul
