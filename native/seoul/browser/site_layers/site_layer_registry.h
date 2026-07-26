// Project Seoul Site Layers.
// Validated store and resolver for per-site visual layers. The registry is the
// product-facing piece above the compiler: layers are accepted only after full
// validation, kept by stable id, and resolved deterministically for a page
// origin plus optional Scene.

#ifndef SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_REGISTRY_H_
#define SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_REGISTRY_H_

#include <map>
#include <string>
#include <vector>

#include "base/values.h"
#include "seoul/browser/site_layers/site_layer_compiler.h"

namespace seoul {

class SiteLayerRegistry {
public:
  SiteLayerRegistry();
  SiteLayerRegistry(const SiteLayerRegistry &) = delete;
  SiteLayerRegistry &operator=(const SiteLayerRegistry &) = delete;
  ~SiteLayerRegistry();

  SiteLayerStatusResult Upsert(SiteLayer layer);
  SiteLayerStatusResult Remove(const std::string &layer_id);
  const SiteLayer *Find(const std::string &layer_id) const;
  bool Exists(const std::string &layer_id) const;
  std::vector<const SiteLayer *> List() const; // id-ordered
  size_t size() const { return layers_.size(); }

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue &state);

  // Compiles all enabled layers matching `origin` and either no Scene scope or
  // exactly `scene_id`. `origin` is the page origin, e.g.
  // "https://example.com".
  SiteLayerResult<std::string>
  CompileForOrigin(const std::string &origin,
                   const std::string &scene_id) const;

  // True when any enabled layer that resolves for this origin/Scene contains
  // `kind`. Used for native behaviors such as automatic dark mode that cannot
  // be represented faithfully as scoped CSS.
  bool HasEnabledAdjustmentForOrigin(const std::string &origin,
                                     const std::string &scene_id,
                                     SiteAdjustmentKind kind) const;

private:
  std::map<std::string, SiteLayer> layers_;
};

bool SiteLayerMatchesOrigin(const SiteLayer &layer, const std::string &origin);

} // namespace seoul

#endif // SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_REGISTRY_H_
