// Project Seoul browser-authoritative cosmetic filtering host.

#include "seoul/browser/adblock/cosmetic_filter_host.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/procedural_cosmetic_sanitizer.h"

namespace seoul::adblock {
namespace {

constexpr size_t kMaxIdentifiersPerKind = 256;
constexpr size_t kMaxIdentifierLength = 256;
constexpr size_t kMaxIdentifierBytes = 64 * 1024;
constexpr size_t kMaxSelectors = 4096;
constexpr size_t kMaxSelectorLength = 2048;
constexpr size_t kMaxSelectorBytes = 512 * 1024;
constexpr size_t kMaxIsolatedScriptBytes = 64 * 1024;
constexpr size_t kMaxCombinedIsolatedScriptBytes = 96 * 1024;

struct SelectorBudget {
  size_t count = 0;
  size_t bytes = 0;
  std::set<std::string> seen;
};

bool IsSafeSelector(std::string_view selector) {
  return !selector.empty() && selector.size() <= kMaxSelectorLength &&
         selector.front() != '@' &&
         selector.find('\0') == std::string_view::npos &&
         selector.find('{') == std::string_view::npos &&
         selector.find('}') == std::string_view::npos;
}

std::vector<std::string> SanitizeIdentifiers(
    const std::vector<std::string>& input) {
  std::vector<std::string> output;
  output.reserve(std::min(input.size(), kMaxIdentifiersPerKind));
  std::set<std::string> seen;
  size_t bytes = 0;
  for (const std::string& identifier : input) {
    if (output.size() == kMaxIdentifiersPerKind || identifier.empty() ||
        identifier.size() > kMaxIdentifierLength ||
        identifier.find('\0') != std::string::npos ||
        bytes + identifier.size() > kMaxIdentifierBytes ||
        !seen.insert(identifier).second) {
      continue;
    }
    bytes += identifier.size();
    output.push_back(identifier);
  }
  return output;
}

std::vector<std::string> SanitizeSelectors(
    const std::vector<std::string>& input,
    SelectorBudget* budget) {
  std::vector<std::string> output;
  for (const std::string& selector : input) {
    if (budget->count == kMaxSelectors || !IsSafeSelector(selector) ||
        budget->bytes + selector.size() > kMaxSelectorBytes ||
        !budget->seen.insert(selector).second) {
      continue;
    }
    ++budget->count;
    budget->bytes += selector.size();
    output.push_back(selector);
  }
  return output;
}

// Procedural actions are sanitized separately by SanitizeProceduralActionSets:
// their budget is shared across both rule groups, so it cannot be applied one
// group at a time here.
mojom::CosmeticSelectorSetPtr ToMojom(AdBlockCosmeticSelectorSet source,
                                      SelectorBudget* selector_budget,
                                      size_t* script_bytes) {
  auto result = mojom::CosmeticSelectorSet::New();
  result->selectors = SanitizeSelectors(source.selectors, selector_budget);
  if (!source.isolated_script.empty() &&
      source.isolated_script.size() <= kMaxIsolatedScriptBytes &&
      *script_bytes + source.isolated_script.size() <=
          kMaxCombinedIsolatedScriptBytes &&
      source.isolated_script.find('\0') == std::string::npos &&
      base::IsStringUTF8(source.isolated_script)) {
    *script_bytes += source.isolated_script.size();
    result->isolated_script = std::move(source.isolated_script);
  }
  result->query_generics = source.query_generics;
  return result;
}

mojom::CosmeticResourcesPtr EmptyResources() {
  auto result = mojom::CosmeticResources::New();
  result->default_rules = mojom::CosmeticSelectorSet::New();
  result->additional_rules = mojom::CosmeticSelectorSet::New();
  return result;
}

mojom::DynamicCosmeticSelectorsPtr EmptyDynamicSelectors() {
  return mojom::DynamicCosmeticSelectors::New();
}

}  // namespace

// static
void CosmeticFilterHost::BindForFrame(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::CosmeticFilterHost> receiver) {
  if (!render_frame_host) {
    return;
  }
  mojo::MakeSelfOwnedReceiver(std::make_unique<CosmeticFilterHost>(
                                  render_frame_host->GetWeakDocumentPtr()),
                              std::move(receiver));
}

CosmeticFilterHost::CosmeticFilterHost(content::WeakDocumentPtr document)
    : document_(std::move(document)) {}

CosmeticFilterHost::~CosmeticFilterHost() = default;

void CosmeticFilterHost::GetCosmeticResources(
    GetCosmeticResourcesCallback callback) {
  content::RenderFrameHost* frame = document_.AsRenderFrameHostIfValid();
  if (!frame) {
    std::move(callback).Run(EmptyResources());
    return;
  }
  const GURL document_url = frame->GetLastCommittedURL();
  Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
  AdBlockService* service =
      profile ? AdBlockServiceFactory::GetForProfile(profile) : nullptr;
  if (!service || !document_url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(EmptyResources());
    return;
  }
  service->GetCosmeticResources(
      document_url,
      base::BindOnce(&CosmeticFilterHost::OnGotCosmeticResources,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CosmeticFilterHost::GetDynamicCosmeticSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    GetDynamicCosmeticSelectorsCallback callback) {
  content::RenderFrameHost* frame = document_.AsRenderFrameHostIfValid();
  if (!frame) {
    std::move(callback).Run(EmptyDynamicSelectors());
    return;
  }
  const GURL document_url = frame->GetLastCommittedURL();
  Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
  AdBlockService* service =
      profile ? AdBlockServiceFactory::GetForProfile(profile) : nullptr;
  if (!service || !document_url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(EmptyDynamicSelectors());
    return;
  }
  service->GetDynamicCosmeticSelectors(
      document_url, SanitizeIdentifiers(classes), SanitizeIdentifiers(ids),
      base::BindOnce(&CosmeticFilterHost::OnGotDynamicCosmeticSelectors,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CosmeticFilterHost::OnGotCosmeticResources(
    GetCosmeticResourcesCallback callback,
    AdBlockCosmeticResources resources) {
  if (!document_.AsRenderFrameHostIfValid()) {
    std::move(callback).Run(EmptyResources());
    return;
  }
  auto result = mojom::CosmeticResources::New();
  SelectorBudget budget;
  size_t script_bytes = 0;
  // Sanitize procedural actions before the sources are moved from. The default
  // group is presented first so it claims the shared budget ahead of the
  // additional group, matching the evaluation order everywhere else.
  SanitizedProceduralActionSets procedural = SanitizeProceduralActionSets(
      resources.default_rules.procedural_actions,
      resources.additional_rules.procedural_actions);
  result->enabled = resources.enabled;
  result->default_rules =
      ToMojom(std::move(resources.default_rules), &budget, &script_bytes);
  result->additional_rules =
      ToMojom(std::move(resources.additional_rules), &budget, &script_bytes);
  result->default_rules->procedural_actions =
      std::move(procedural.default_actions);
  result->additional_rules->procedural_actions =
      std::move(procedural.additional_actions);
  std::move(callback).Run(std::move(result));
}

void CosmeticFilterHost::OnGotDynamicCosmeticSelectors(
    GetDynamicCosmeticSelectorsCallback callback,
    AdBlockDynamicCosmeticSelectors selectors) {
  if (!document_.AsRenderFrameHostIfValid()) {
    std::move(callback).Run(EmptyDynamicSelectors());
    return;
  }
  auto result = mojom::DynamicCosmeticSelectors::New();
  SelectorBudget budget;
  result->default_selectors =
      SanitizeSelectors(selectors.default_selectors, &budget);
  result->additional_selectors =
      SanitizeSelectors(selectors.additional_selectors, &budget);
  std::move(callback).Run(std::move(result));
}

}  // namespace seoul::adblock
