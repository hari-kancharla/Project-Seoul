// Project Seoul product runtime: the provider registry.
// Owns the optional local and cloud reasoning providers, their settings,
// health, and the structured-plan request path the planner uses. Provider
// names never leak into generic core APIs: callers speak in routes
// (local/cloud), and the registry maps routes to whatever is configured.
//
// STATE OWNERSHIP
//   owner:        one ProviderRegistry per profile runtime.
//   lifetime:     the profile runtime.
//   persistence:  settings (endpoint, model ids, enabled flags) serialize
//                 through TakePersistedState()/Restore; secrets NEVER pass
//                 through here - they live in the CredentialStore only.
//   recovery:     providers are reconstructed from settings on restore.
//   teardown:     Shutdown() cancels in-progress generations.
//   bounds:       at most one local and one cloud provider in V1.
//   isolation:    per profile.

#ifndef SEOUL_BROWSER_PRODUCT_PROVIDER_REGISTRY_H_
#define SEOUL_BROWSER_PRODUCT_PROVIDER_REGISTRY_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "seoul/browser/intelligence/credential_store.h"
#include "seoul/browser/intelligence/http_transport.h"
#include "seoul/browser/intelligence/model_provider.h"
#include "seoul/browser/product/planner.h"

namespace seoul {

inline constexpr char kCloudReasoningCredentialAccount[] = "cloud_reasoning";

struct ProviderStateSnapshot {
  ProviderStateSnapshot();
  ProviderStateSnapshot(const ProviderStateSnapshot&);
  ProviderStateSnapshot(ProviderStateSnapshot&&);
  ProviderStateSnapshot& operator=(const ProviderStateSnapshot&);
  ProviderStateSnapshot& operator=(ProviderStateSnapshot&&);
  ~ProviderStateSnapshot();

  bool local_configured = false;
  bool local_healthy = false;
  std::string local_endpoint;
  std::string local_model;
  std::vector<std::string> local_models_discovered;
  bool cloud_configured = false;  // credential present
  bool cloud_enabled = false;     // user switch
  std::string cloud_model;
  std::string last_error;
};

class ProviderRegistry {
 public:
  // `local_transport` must enforce loopback-only; `cloud_transport` is the
  // general network transport; `credentials` is the OS-backed secret store.
  // All must outlive this registry (the runtime owns them).
  ProviderRegistry(HttpTransport* local_transport,
                   HttpTransport* cloud_transport,
                   CredentialStore* credentials);
  ProviderRegistry(const ProviderRegistry&) = delete;
  ProviderRegistry& operator=(const ProviderRegistry&) = delete;
  ~ProviderRegistry();

  // Settings. Secrets are set/deleted directly on the credential store by
  // the runtime service; this registry only learns whether one exists.
  bool ConfigureLocal(const std::string& endpoint_url,
                      const std::string& model_id);
  void ClearLocal();
  bool ConfigureCloud(const std::string& model_id, bool enabled);
  void SetCloudEnabled(bool enabled);
  void ClearCloud();

  // Health check against the local endpoint (GET <endpoint>/models). Also
  // refreshes the discovered model list. The callback reports reachability.
  void CheckLocalHealth(base::OnceCallback<void(bool healthy)> callback);

  ProviderStateSnapshot Snapshot() const;

  // True when an allowed reasoning provider is usable right now. A
  // cloud-disabled Scene can still use a healthy local provider, but this
  // method never treats the cloud route as eligible when
  // `allow_cloud_models` is false.
  bool HasUsableProvider(bool allow_cloud_models = true) const;
  bool local_available() const { return local_provider_ && local_healthy_; }
  bool cloud_available() const;

  // The planner's model seam: routes a structured plan request to the local
  // or cloud provider (prefer_local first, falling back to the other route
  // when the preferred one is absent).
  ModelPlanRequester MakePlanRequester();

  // A plain generation entry point for conversational turns.
  void Generate(const GenerationRequest& request,
                bool prefer_local,
                ModelProvider::GenerateCallback callback);

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue& state);

  void Shutdown();

 private:
  ModelProvider* PickProvider(bool prefer_local,
                              bool allow_cloud_models) const;
  void RequestPlan(const std::string& prompt,
                   bool prefer_local,
                   bool allow_cloud_models,
                   base::OnceCallback<void(std::optional<base::DictValue>,
                                           PlanOrigin)> callback);
  void OnPlanGenerated(base::OnceCallback<void(std::optional<base::DictValue>,
                                               PlanOrigin)> callback,
                       PlanOrigin origin,
                       base::expected<GenerationResult, std::string> result);
  void OnHealthResponse(base::OnceCallback<void(bool)> callback,
                        std::string body,
                        int http_status,
                        const std::string& transport_error);

  raw_ptr<HttpTransport> local_transport_;
  raw_ptr<HttpTransport> cloud_transport_;
  raw_ptr<CredentialStore> credentials_;
  std::unique_ptr<ModelProvider> local_provider_;
  std::unique_ptr<ModelProvider> cloud_provider_;
  std::string local_endpoint_;
  std::string local_model_;
  std::string cloud_model_;
  bool cloud_enabled_ = false;
  bool local_healthy_ = false;
  std::vector<std::string> local_models_discovered_;
  std::string last_error_;
  bool shutting_down_ = false;
  base::WeakPtrFactory<ProviderRegistry> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_PROVIDER_REGISTRY_H_
