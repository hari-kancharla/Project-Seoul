// Project Seoul Scenes.
// Validation, storage, and activation planning for Scenes. A Scene references
// entities in other subsystems by id; the registry validates those references
// against resolver callbacks (so it never inlines or duplicates their state)
// and produces a deterministic, ordered activation plan describing exactly
// what to apply.

#ifndef SEOUL_BROWSER_SCENES_SCENE_REGISTRY_H_
#define SEOUL_BROWSER_SCENES_SCENE_REGISTRY_H_

#include <map>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "seoul/browser/scenes/scene_types.h"

namespace seoul {

// Existence checks into the other subsystems (organization, themes, site
// layers). Returning false rejects the reference at validation time.
struct SceneResolvers {
  SceneResolvers();
  SceneResolvers(const SceneResolvers &);
  SceneResolvers(SceneResolvers &&);
  SceneResolvers &operator=(const SceneResolvers &);
  SceneResolvers &operator=(SceneResolvers &&);
  ~SceneResolvers();

  base::RepeatingCallback<bool(const std::string &workspace_id)>
      workspace_exists;
  base::RepeatingCallback<bool(const std::string &theme_id)> theme_exists;
  base::RepeatingCallback<bool(const std::string &site_layer_id)>
      site_layer_exists;
  base::RepeatingCallback<bool(const std::string &routing_rule_id)>
      routing_rule_exists;
  base::RepeatingCallback<bool(const std::string &workflow_id)> workflow_exists;
};

// One ordered step of activation. The shell/services execute these in order;
// the plan itself performs no mutation.
struct SceneActivationStep {
  enum class Kind {
    kSwitchWorkspace,
    kApplyTheme,
    kEnableSiteLayer,
    kInstallRoutingRule,
    kApplyLifecyclePolicy,
    kApplyAssistantDefaults,
    kSetCompactPreference,
  };
  Kind kind;
  std::string target_id; // workspace/theme/layer/rule id, when applicable
};

class SceneRegistry {
public:
  explicit SceneRegistry(SceneResolvers resolvers);
  SceneRegistry(const SceneRegistry &) = delete;
  SceneRegistry &operator=(const SceneRegistry &) = delete;
  ~SceneRegistry();

  // Validates references and identity, then stores (or replaces) the Scene.
  SceneStatusResult Upsert(SceneDefinition scene);
  SceneStatusResult Remove(const std::string &scene_id);
  const SceneDefinition *Find(const std::string &scene_id) const;
  std::vector<const SceneDefinition *> List() const; // id-ordered
  size_t size() const { return scenes_.size(); }

  // Rebinds cross-service resolvers before restoring or accepting authored
  // Scenes. Existing entries are not modified; callers revalidate them before
  // activation and guard dependent deletions at the runtime boundary.
  void SetResolvers(SceneResolvers resolvers);

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue &state);
  // Revalidates stored Scenes against the current resolver catalogs and
  // removes entries whose references became invalid.
  size_t PruneInvalidEntries();

  // Validates a Scene against the resolvers without storing it.
  SceneStatusResult Validate(const SceneDefinition &scene) const;

  // Deterministic activation plan for a stored Scene: workspace switch first,
  // then theme, site layers (in declared order), routing rules, lifecycle,
  // assistant defaults, compact preference.
  SceneResult<std::vector<SceneActivationStep>>
  BuildActivationPlan(const std::string &scene_id) const;

private:
  SceneResolvers resolvers_;
  std::map<std::string, SceneDefinition> scenes_;
};

} // namespace seoul

#endif // SEOUL_BROWSER_SCENES_SCENE_REGISTRY_H_
