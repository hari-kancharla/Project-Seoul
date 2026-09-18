// Project Seoul hybrid intelligence.
// An official cloud model provider using a messages-style API with a
// bring-your-own-key credential. The API key is fetched from the injected
// CredentialStore immediately before each request and never retained, logged,
// or exposed to the Canvas. Streams via SSE, reports usage and estimated cost,
// and maps provider errors to readable messages. The transport and credential
// store are injected, so the provider is testable without a network or the
// real Keychain.

#ifndef SEOUL_BROWSER_INTELLIGENCE_CLOUD_MODEL_PROVIDER_H_
#define SEOUL_BROWSER_INTELLIGENCE_CLOUD_MODEL_PROVIDER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/intelligence/credential_store.h"
#include "seoul/browser/intelligence/http_transport.h"
#include "seoul/browser/intelligence/model_provider.h"
#include "seoul/browser/intelligence/streaming_accumulator.h"

namespace seoul {

struct CloudModelConfig {
  CloudModelConfig();
  CloudModelConfig(const CloudModelConfig&);
  CloudModelConfig(CloudModelConfig&&);
  CloudModelConfig& operator=(const CloudModelConfig&);
  CloudModelConfig& operator=(CloudModelConfig&&);
  ~CloudModelConfig();

  std::string endpoint_url;
  std::string model_id;
  std::string api_version_header;  // e.g. a version pin sent as a header
  std::string credential_account;  // key into the CredentialStore (not a key)
  ModelCapabilities capabilities;
};

class CloudModelProvider : public ModelProvider {
 public:
  CloudModelProvider(CloudModelConfig config,
                     HttpTransport* transport,
                     CredentialStore* credentials);
  ~CloudModelProvider() override;

  std::string provider_id() const override;
  ModelCapabilities capabilities() const override;
  void Generate(const GenerationRequest& request,
                GenerateCallback callback) override;
  void Cancel() override;
  int64_t EstimateCostMicrodollars(int input_tokens,
                                   int output_tokens) const override;

 private:
  CloudModelConfig config_;
  raw_ptr<HttpTransport> transport_;
  raw_ptr<CredentialStore> credentials_;
  std::unique_ptr<StreamingAccumulator> accumulator_;
  GenerateCallback pending_;
  int active_handle_ = 0;
  base::WeakPtrFactory<CloudModelProvider> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_INTELLIGENCE_CLOUD_MODEL_PROVIDER_H_
