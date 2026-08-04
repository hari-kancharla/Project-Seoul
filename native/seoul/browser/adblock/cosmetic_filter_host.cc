// Project Seoul browser-authoritative cosmetic filtering host.

#include "seoul/browser/adblock/cosmetic_filter_host.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"

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
constexpr size_t kMaxProceduralActions = 64;
constexpr size_t kMaxProceduralActionBytes = 4096;
constexpr size_t kMaxCombinedProceduralActionBytes = 128 * 1024;
constexpr size_t kMaxProceduralOperators = 8;
constexpr size_t kMaxProceduralTextBytes = 256;

struct SelectorBudget {
  size_t count = 0;
  size_t bytes = 0;
  std::set<std::string> seen;
};

struct ProceduralBudget {
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

bool IsSafeIdentifier(std::string_view identifier, bool allow_colon_and_dot) {
  if (identifier.empty() || identifier.size() > 128) {
    return false;
  }
  for (const char character : identifier) {
    if (base::IsAsciiAlphaNumeric(character) || character == '-' ||
        character == '_' ||
        (allow_colon_and_dot && (character == ':' || character == '.'))) {
      continue;
    }
    return false;
  }
  return true;
}

std::optional<base::DictValue> SanitizeProceduralOperator(
    const base::Value& value) {
  const base::DictValue* source = value.GetIfDict();
  if (!source || source->size() != 2u) {
    return std::nullopt;
  }
  const std::string* type = source->FindString("type");
  const std::string* argument = source->FindString("arg");
  if (!type || !argument || argument->find('\0') != std::string::npos) {
    return std::nullopt;
  }

  bool valid = false;
  if (*type == "css-selector") {
    valid = IsSafeSelector(*argument);
  } else if (*type == "has-text") {
    // Regex input is deliberately deferred: accepting only literal text avoids
    // list-controlled catastrophic backtracking in the renderer.
    valid = !argument->empty() && argument->size() <= kMaxProceduralTextBytes &&
            argument->front() != '/';
  } else if (*type == "min-text-length") {
    int minimum_length = 0;
    valid = base::StringToInt(*argument, &minimum_length) &&
            minimum_length >= 0 && minimum_length <= 100000;
  } else if (*type == "upward") {
    int levels = 0;
    valid =
        base::StringToInt(*argument, &levels) && levels >= 1 && levels <= 16;
  }
  if (!valid) {
    return std::nullopt;
  }

  base::DictValue sanitized;
  sanitized.Set("type", *type);
  sanitized.Set("arg", *argument);
  return sanitized;
}

std::optional<base::DictValue> SanitizeProceduralActionObject(
    const base::DictValue& source) {
  const std::string* type = source.FindString("type");
  if (!type) {
    return std::nullopt;
  }
  base::DictValue sanitized;
  if (*type == "remove" && source.size() == 1u) {
    sanitized.Set("type", *type);
    return sanitized;
  }
  if (source.size() != 2u) {
    return std::nullopt;
  }
  const std::string* argument = source.FindString("arg");
  if (!argument) {
    return std::nullopt;
  }
  if (*type == "remove-attr" &&
      IsSafeIdentifier(*argument, /*allow_colon_and_dot=*/true)) {
    sanitized.Set("type", *type);
    sanitized.Set("arg", *argument);
    return sanitized;
  }
  if (*type == "remove-class" &&
      IsSafeIdentifier(*argument, /*allow_colon_and_dot=*/false)) {
    sanitized.Set("type", *type);
    sanitized.Set("arg", *argument);
    return sanitized;
  }
  return std::nullopt;
}

std::optional<std::string> SanitizeProceduralAction(
    std::string_view serialized) {
  if (serialized.empty() || serialized.size() > kMaxProceduralActionBytes ||
      serialized.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(serialized)) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(serialized, /*options=*/0, /*max_depth=*/16);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& source = parsed->GetDict();
  const base::ListValue* operators = source.FindList("selector");
  const base::DictValue* action = source.FindDict("action");
  if (!operators || operators->empty() ||
      operators->size() > kMaxProceduralOperators ||
      source.size() != (action ? 2u : 1u)) {
    return std::nullopt;
  }

  base::ListValue sanitized_operators;
  for (const base::Value& value : *operators) {
    std::optional<base::DictValue> sanitized =
        SanitizeProceduralOperator(value);
    if (!sanitized) {
      return std::nullopt;
    }
    sanitized_operators.Append(std::move(*sanitized));
  }
  base::DictValue sanitized;
  sanitized.Set("selector", std::move(sanitized_operators));
  if (action) {
    std::optional<base::DictValue> sanitized_action =
        SanitizeProceduralActionObject(*action);
    if (!sanitized_action) {
      return std::nullopt;
    }
    sanitized.Set("action", std::move(*sanitized_action));
  }
  return base::WriteJson(sanitized);
}

std::vector<std::string> SanitizeProceduralActions(
    const std::vector<std::string>& input,
    ProceduralBudget* budget) {
  std::vector<std::string> output;
  for (const std::string& serialized : input) {
    if (budget->count == kMaxProceduralActions) {
      break;
    }
    std::optional<std::string> sanitized = SanitizeProceduralAction(serialized);
    if (!sanitized ||
        budget->bytes + sanitized->size() > kMaxCombinedProceduralActionBytes ||
        !budget->seen.insert(*sanitized).second) {
      continue;
    }
    ++budget->count;
    budget->bytes += sanitized->size();
    output.push_back(std::move(*sanitized));
  }
  return output;
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

mojom::CosmeticSelectorSetPtr ToMojom(AdBlockCosmeticSelectorSet source,
                                      SelectorBudget* selector_budget,
                                      size_t* script_bytes,
                                      ProceduralBudget* procedural_budget) {
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
  result->procedural_actions =
      SanitizeProceduralActions(source.procedural_actions, procedural_budget);
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
  ProceduralBudget procedural_budget;
  result->enabled = resources.enabled;
  result->default_rules = ToMojom(std::move(resources.default_rules), &budget,
                                  &script_bytes, &procedural_budget);
  result->additional_rules =
      ToMojom(std::move(resources.additional_rules), &budget, &script_bytes,
              &procedural_budget);
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
