// Project Seoul browser-authoritative cosmetic filtering host.

#ifndef SEOUL_BROWSER_ADBLOCK_COSMETIC_FILTER_HOST_H_
#define SEOUL_BROWSER_ADBLOCK_COSMETIC_FILTER_HOST_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/weak_document_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "seoul/browser/adblock/ad_block_engine_host.h"
#include "seoul/browser/adblock/cosmetic_filter.mojom.h"

namespace content {
class RenderFrameHost;
}

namespace seoul::adblock {

class CosmeticFilterHost final : public mojom::CosmeticFilterHost {
 public:
  static void BindForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<mojom::CosmeticFilterHost> receiver);

  explicit CosmeticFilterHost(content::WeakDocumentPtr document);
  ~CosmeticFilterHost() override;

  CosmeticFilterHost(const CosmeticFilterHost&) = delete;
  CosmeticFilterHost& operator=(const CosmeticFilterHost&) = delete;

  // mojom::CosmeticFilterHost:
  void GetCosmeticResources(GetCosmeticResourcesCallback callback) override;
  void GetDynamicCosmeticSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      GetDynamicCosmeticSelectorsCallback callback) override;

 private:
  void OnGotCosmeticResources(GetCosmeticResourcesCallback callback,
                              AdBlockCosmeticResources resources);
  void OnGotDynamicCosmeticSelectors(
      GetDynamicCosmeticSelectorsCallback callback,
      AdBlockDynamicCosmeticSelectors selectors);

  content::WeakDocumentPtr document_;
  base::WeakPtrFactory<CosmeticFilterHost> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_COSMETIC_FILTER_HOST_H_
