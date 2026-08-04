// Project Seoul asynchronous, CSS-only cosmetic filtering agent.

#ifndef SEOUL_RENDERER_COSMETIC_FILTER_AGENT_H_
#define SEOUL_RENDERER_COSMETIC_FILTER_AGENT_H_

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "seoul/browser/adblock/cosmetic_filter.mojom.h"
#include "third_party/blink/public/web/web_document.h"

namespace content {
class RenderFrame;
}

namespace seoul::renderer {

class CosmeticFilterAgent final : public content::RenderFrameObserver {
 public:
  static void Create(content::RenderFrame* render_frame);

  explicit CosmeticFilterAgent(content::RenderFrame* render_frame);
  ~CosmeticFilterAgent() override;

  CosmeticFilterAgent(const CosmeticFilterAgent&) = delete;
  CosmeticFilterAgent& operator=(const CosmeticFilterAgent&) = delete;

  // content::RenderFrameObserver:
  void DidCreateNewDocument() override;
  void DidCreateDocumentElement() override;
  void DidSetPageLifecycleState(
      blink::BFCacheStateChange bfcache_change) override;
  void OnDestruct() override;

 private:
  void ClearForNewDocument();
  void SuspendForBackForwardCache();
  void BeginForCurrentDocument(bool refresh);
  void RequestResources(uint64_t generation, bool refresh);
  void OnGotResources(uint64_t generation,
                      bool refresh,
                      adblock::mojom::CosmeticResourcesPtr resources);

  void InstallDiscoveryScript();
  void RemoveDiscoveryScript();
  void PollIdentifiers();
  void OnGotDynamicSelectors(
      uint64_t generation,
      adblock::mojom::DynamicCosmeticSelectorsPtr selectors);

  void ReplaceSelectors(const std::vector<std::string>& default_selectors,
                        const std::vector<std::string>& additional_selectors);
  void ExecuteIsolatedScript(const std::string& script);
  void InstallProceduralRules(
      const std::vector<std::string>& default_actions,
      const std::vector<std::string>& additional_actions);
  void RemoveProceduralRules();
  bool AppendSelectors(const std::vector<std::string>& selectors);
  void ApplyStyleSheet();
  void RemoveStyleSheet();

  uint64_t generation_ = 0;
  bool suspended_ = false;
  bool document_request_started_ = false;
  bool request_in_flight_ = false;
  bool discovery_installed_ = false;
  bool procedural_rules_installed_ = false;
  bool style_sheet_inserted_ = false;
  bool query_generics_ = false;
  size_t style_sheet_bytes_ = 0;
  std::set<std::string> selectors_;
  std::set<std::string> executed_isolated_scripts_;
  std::string style_sheet_;
  blink::WebStyleSheetKey style_sheet_key_;
  base::RepeatingTimer poll_timer_;
  mojo::Remote<adblock::mojom::CosmeticFilterHost> host_;
  base::WeakPtrFactory<CosmeticFilterAgent> weak_factory_{this};
};

}  // namespace seoul::renderer

#endif  // SEOUL_RENDERER_COSMETIC_FILTER_AGENT_H_
