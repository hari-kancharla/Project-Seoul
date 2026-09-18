// Project Seoul hybrid intelligence.
// A local, chat-completions-compatible model provider (for example a local
// runtime exposing a chat-completions HTTP endpoint on loopback). It enforces a
// local-only endpoint by default so on-device reasoning can never be pointed
// at a remote host, streams via SSE, and reports usage. The HttpTransport is
// injected, so the provider is testable without a network; the concrete
// loopback transport is a capable-host glue target.

#ifndef SEOUL_BROWSER_INTELLIGENCE_LOCAL_MODEL_PROVIDER_H_
#define SEOUL_BROWSER_INTELLIGENCE_LOCAL_MODEL_PROVIDER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/intelligence/http_transport.h"
#include "seoul/browser/intelligence/model_provider.h"
#include "seoul/browser/intelligence/streaming_accumulator.h"

namespace seoul {

struct LocalModelConfig {
  LocalModelConfig();
  LocalModelConfig(const LocalModelConfig&);
  LocalModelConfig(LocalModelConfig&&);
  LocalModelConfig& operator=(const LocalModelConfig&);
  LocalModelConfig& operator=(LocalModelConfig&&);
  ~LocalModelConfig();

  std::string endpoint_url;  // must be loopback (enforced)
  std::string model_id;      // discovered/selected local model
  ModelCapabilities capabilities;
};

class LocalModelProvider : public ModelProvider {
 public:
  // `transport` outlives the provider (owned by the runtime).
  LocalModelProvider(LocalModelConfig config, HttpTransport* transport);
  ~LocalModelProvider() override;

  std::string provider_id() const override;
  ModelCapabilities capabilities() const override;
  void Generate(const GenerationRequest& request,
                GenerateCallback callback) override;
  void Cancel() override;
  int64_t EstimateCostMicrodollars(int input_tokens,
                                   int output_tokens) const override;

 private:
  LocalModelConfig config_;
  raw_ptr<HttpTransport> transport_;
  std::unique_ptr<StreamingAccumulator> accumulator_;
  GenerateCallback pending_;
  int active_handle_ = 0;
  base::WeakPtrFactory<LocalModelProvider> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_INTELLIGENCE_LOCAL_MODEL_PROVIDER_H_
