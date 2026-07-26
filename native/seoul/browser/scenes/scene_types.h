// Project Seoul Scenes.
// A Scene is a reusable browsing environment: it references (never inlines) a
// workspace, a theme, a set of Site Layers, routing rules, a tab-lifecycle
// policy, assistant context defaults, and workflow shortcuts. Scenes are
// composed of ids into the other subsystems so a Scene never duplicates their
// state; activation applies the referenced pieces atomically.

#ifndef SEOUL_BROWSER_SCENES_SCENE_TYPES_H_
#define SEOUL_BROWSER_SCENES_SCENE_TYPES_H_

#include <string>
#include <vector>

#include "base/types/expected.h"
#include "seoul/browser/tools/tool_types.h"

namespace seoul {

inline constexpr int kSceneSchemaVersion = 1;
inline constexpr size_t kMaxScenes = 100;
inline constexpr size_t kMaxSceneNameLength = 120;
inline constexpr size_t kMaxSceneSiteLayers = 64;
inline constexpr size_t kMaxSceneRoutingRules = 64;
inline constexpr size_t kMaxSceneWorkflowShortcuts = 32;
inline constexpr size_t kMaxSceneContextTools = 64;
inline constexpr size_t kMaxSceneReferenceLength = 128;

// Tab lifecycle policy defaults a Scene applies (bounds live in the
// organization model; a Scene only chooses within them).
struct SceneLifecyclePolicy {
  bool archive_temporary_tabs = true;
  int idle_archive_minutes = 60;
  bool restore_on_activation = true;

  friend bool operator==(const SceneLifecyclePolicy &,
                         const SceneLifecyclePolicy &) = default;
};

// Assistant context defaults: what the operator may reach by default in this
// Scene (still capped by the user's global permission context at run time).
struct SceneAssistantDefaults {
  SceneAssistantDefaults();
  SceneAssistantDefaults(const SceneAssistantDefaults &);
  SceneAssistantDefaults(SceneAssistantDefaults &&);
  SceneAssistantDefaults &operator=(const SceneAssistantDefaults &);
  SceneAssistantDefaults &operator=(SceneAssistantDefaults &&);
  ~SceneAssistantDefaults();

  bool allow_network = false;
  bool allow_cloud_models = false;
  DataSensitivity max_sensitivity = DataSensitivity::kOrganization;
  // Connector providers auto-attached to tasks started in this Scene.
  std::vector<std::string> default_connectors;

  friend bool operator==(const SceneAssistantDefaults &,
                         const SceneAssistantDefaults &) = default;
};

struct SceneDefinition {
  SceneDefinition();
  SceneDefinition(const SceneDefinition &);
  SceneDefinition(SceneDefinition &&);
  SceneDefinition &operator=(const SceneDefinition &);
  SceneDefinition &operator=(SceneDefinition &&);
  ~SceneDefinition();

  int schema_version = kSceneSchemaVersion;
  std::string id;
  std::string name;
  std::string workspace_id; // referenced organization workspace
  std::string theme_id;     // referenced theme (empty: global theme)
  std::vector<std::string> site_layer_ids;
  std::vector<std::string> routing_rule_ids;
  std::vector<std::string> workflow_shortcut_ids;
  SceneLifecyclePolicy lifecycle;
  SceneAssistantDefaults assistant;
  bool prefer_compact = false;

  friend bool operator==(const SceneDefinition &,
                         const SceneDefinition &) = default;
};

enum class SceneError {
  kInvalidName,
  kInvalidId,
  kMissingWorkspace,
  kTooManySceneItems,
  kDuplicateReference,
  kUnknownScene,
  kUnknownWorkspace,
  kUnknownTheme,
  kUnknownSiteLayer,
  kUnknownRoutingRule,
  kUnknownWorkflow,
  kInvalidLifecyclePolicy,
  kUnsupportedSchema,
  kLimitExceeded,
  kInUse,
  kActivationFailed,
};

const char *SceneErrorToString(SceneError error);

template <typename T> using SceneResult = base::expected<T, SceneError>;

using SceneStatusResult = base::expected<void, SceneError>;

} // namespace seoul

#endif // SEOUL_BROWSER_SCENES_SCENE_TYPES_H_
