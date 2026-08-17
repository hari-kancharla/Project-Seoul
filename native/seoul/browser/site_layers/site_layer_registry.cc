// Project Seoul Site Layers.

#include "seoul/browser/site_layers/site_layer_registry.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

#include "base/strings/string_util.h"
#include "seoul/browser/site_layers/site_layer_compiler.h"
#include "url/gurl.h"

namespace seoul {

namespace {

constexpr int kSiteLayerRegistrySchemaVersion = 1;

struct OriginPatternParts {
  bool wildcard = false;
  std::string scheme;
  std::string host;
  std::string port;
};

std::optional<OriginPatternParts> ParseOriginPattern(const std::string &origin,
                                                     bool allow_wildcard) {
  if (!IsValidOriginPattern(origin)) {
    return std::nullopt;
  }
  OriginPatternParts parts;
  std::string host;
  if (base::StartsWith(origin, "*.")) {
    if (!allow_wildcard) {
      return std::nullopt;
    }
    parts.wildcard = true;
    host = origin.substr(2);
  } else {
    const GURL url(origin);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      return std::nullopt;
    }
    parts.scheme = url.scheme();
    parts.host = base::ToLowerASCII(url.host());
    parts.port = url.port();
    return parts;
  }
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    parts.port = host.substr(colon + 1);
    host = host.substr(0, colon);
  }
  parts.host = base::ToLowerASCII(host);
  return parts;
}

bool HostMatchesWildcard(const std::string &host,
                         const std::string &wildcard_host) {
  return host == wildcard_host ||
         (host.size() > wildcard_host.size() &&
          base::EndsWith(host, "." + wildcard_host,
                         base::CompareCase::SENSITIVE));
}

} // namespace

SiteLayerRegistry::SiteLayerRegistry() = default;

SiteLayerRegistry::~SiteLayerRegistry() = default;

SiteLayerStatusResult SiteLayerRegistry::Upsert(SiteLayer layer) {
  if (auto valid = ValidateSiteLayer(layer); !valid.has_value()) {
    return valid;
  }
  const bool exists = layers_.find(layer.id) != layers_.end();
  if (!exists && layers_.size() >= kMaxSiteLayers) {
    return base::unexpected(SiteLayerError::kLimitExceeded);
  }
  const std::string id = layer.id;
  layers_[id] = std::move(layer);
  return base::ok();
}

SiteLayerStatusResult SiteLayerRegistry::Remove(const std::string &layer_id) {
  if (layers_.erase(layer_id) == 0) {
    return base::unexpected(SiteLayerError::kUnknownLayer);
  }
  return base::ok();
}

const SiteLayer *SiteLayerRegistry::Find(const std::string &layer_id) const {
  auto it = layers_.find(layer_id);
  return it == layers_.end() ? nullptr : &it->second;
}

bool SiteLayerRegistry::Exists(const std::string &layer_id) const {
  return Find(layer_id) != nullptr;
}

std::vector<const SiteLayer *> SiteLayerRegistry::List() const {
  std::vector<const SiteLayer *> result;
  for (const auto &[id, layer] : layers_) {
    result.push_back(&layer);
  }
  return result;
}

base::DictValue SiteLayerRegistry::TakePersistedState() const {
  base::DictValue state;
  state.Set("schema_version", kSiteLayerRegistrySchemaVersion);
  base::ListValue layers;
  for (const auto &[id, layer] : layers_) {
    layers.Append(SiteLayerToValue(layer));
  }
  state.Set("site_layers", std::move(layers));
  return state;
}

void SiteLayerRegistry::RestorePersistedState(const base::DictValue &state) {
  if (state.FindInt("schema_version").value_or(0) !=
      kSiteLayerRegistrySchemaVersion) {
    return;
  }
  const base::ListValue *layers = state.FindList("site_layers");
  if (!layers) {
    return;
  }
  SiteLayerRegistry restored;
  for (const base::Value &entry : *layers) {
    if (restored.size() >= kMaxSiteLayers) {
      break;
    }
    SiteLayerResult<SiteLayer> layer = SiteLayerFromValue(entry);
    if (layer.has_value()) {
      std::ignore = restored.Upsert(std::move(layer.value()));
    }
  }
  layers_ = std::move(restored.layers_);
}

SiteLayerResult<std::string>
SiteLayerRegistry::CompileForOrigin(const std::string &origin,
                                    const std::string &scene_id) const {
  if (!ParseOriginPattern(origin, /*allow_wildcard=*/false).has_value()) {
    return base::unexpected(SiteLayerError::kInvalidOrigin);
  }
  std::string css;
  for (const auto &[id, layer] : layers_) {
    if (!layer.enabled || !SiteLayerMatchesOrigin(layer, origin)) {
      continue;
    }
    if (!layer.scene_scope.empty() && layer.scene_scope != scene_id) {
      continue;
    }
    auto compiled = CompileSiteLayer(layer);
    if (!compiled.has_value()) {
      return base::unexpected(compiled.error());
    }
    css += compiled.value();
  }
  return css;
}

std::string SiteLayerRegistry::CustomJavaScriptForOrigin(
    const std::string &origin, const std::string &scene_id) const {
  if (!ParseOriginPattern(origin, /*allow_wildcard=*/false).has_value()) {
    return std::string();
  }
  std::string script;
  for (const auto &[id, layer] : layers_) {
    if (!layer.enabled || layer.custom_javascript.empty() ||
        !SiteLayerMatchesOrigin(layer, origin)) {
      continue;
    }
    if (!layer.scene_scope.empty() && layer.scene_scope != scene_id) {
      continue;
    }
    // Each author's script is wrapped on its own so one that throws cannot
    // stop the next Boost's from running.
    script += "try { (function(){\n";
    script += layer.custom_javascript;
    script += "\n})(); } catch (e) {}\n";
  }
  return script;
}

bool SiteLayerRegistry::HasEnabledAdjustmentForOrigin(
    const std::string &origin, const std::string &scene_id,
    SiteAdjustmentKind kind) const {
  if (!ParseOriginPattern(origin, /*allow_wildcard=*/false).has_value()) {
    return false;
  }
  for (const auto &[id, layer] : layers_) {
    if (!layer.enabled || !SiteLayerMatchesOrigin(layer, origin) ||
        (!layer.scene_scope.empty() && layer.scene_scope != scene_id)) {
      continue;
    }
    if (std::ranges::any_of(layer.adjustments,
                            [kind](const SiteAdjustment &adjustment) {
                              return adjustment.kind == kind;
                            })) {
      return true;
    }
  }
  return false;
}

bool SiteLayerMatchesOrigin(const SiteLayer &layer, const std::string &origin) {
  std::optional<OriginPatternParts> pattern =
      ParseOriginPattern(layer.origin_pattern, /*allow_wildcard=*/true);
  std::optional<OriginPatternParts> page =
      ParseOriginPattern(origin, /*allow_wildcard=*/false);
  if (!pattern.has_value() || !page.has_value()) {
    return false;
  }
  if (pattern->wildcard) {
    return page->port.empty() && HostMatchesWildcard(page->host, pattern->host);
  }
  return page->scheme == pattern->scheme && page->host == pattern->host &&
         page->port == pattern->port;
}

} // namespace seoul
