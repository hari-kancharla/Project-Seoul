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
  void DidObserveLoadingBehavior(blink::LoadingBehaviorFlag behavior) override;
  void WillDetach(blink::DetachReason detach_reason) override;
  void OnDestruct() override;

 private:
  void ClearForNewDocument();
  // Sends the farbled readbacks counted since the last report. Batched,
  // because a canvas-heavy page reads back many times a second and the
  // receipt needs counts, not a message per read.
  void FlushFarbledReads();
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
  // Writes browser-approved `:style()` rules into the same stylesheet, under
  // the same byte cap, after re-validating the declarations. The renderer does
  // not trust the pipe: a compromised browser process is out of scope, but a
  // bug on the other side must not become a CSS injection here.
  bool AppendStyledSelectors(
      const std::vector<adblock::mojom::StyledSelectorPtr>& styled);
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
