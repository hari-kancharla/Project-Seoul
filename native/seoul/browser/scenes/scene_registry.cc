// Project Seoul Scenes.

#include "seoul/browser/scenes/scene_registry.h"

#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include "seoul/browser/tools/tool_descriptor_wire.h"

namespace seoul {

SceneResolvers::SceneResolvers() = default;
SceneResolvers::SceneResolvers(const SceneResolvers &) = default;
SceneResolvers::SceneResolvers(SceneResolvers &&) = default;
SceneResolvers &SceneResolvers::operator=(const SceneResolvers &) = default;
SceneResolvers &SceneResolvers::operator=(SceneResolvers &&) = default;
SceneResolvers::~SceneResolvers() = default;

namespace {

constexpr int kSceneRegistrySchemaVersion = 1;

bool ValidSlug(const std::string &slug, size_t max_length) {
  if (slug.empty() || slug.size() > max_length) {
    return false;
  }
  if (slug[0] < 'a' || slug[0] > 'z') {
    return false;
  }
  for (char c : slug) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-')) {
      return false;
    }
  }
  return true;
}

template <typename T> bool HasDuplicates(const std::vector<T> &values) {
  std::set<T> seen;
  for (const T &value : values) {
    if (!seen.insert(value).second) {
      return true;
    }
  }
  return false;
}

bool AllBoundedReferences(const std::vector<std::string> &values) {
  for (const std::string &value : values) {
    if (value.empty() || value.size() > kMaxSceneReferenceLength) {
      return false;
    }
  }
  return true;
}

base::ListValue StringListToValue(const std::vector<std::string> &values) {
  base::ListValue list;
  for (const std::string &value : values) {
    list.Append(value);
  }
  return list;
}

bool ReadStringList(const base::DictValue &value, const char *key,
                    size_t maximum, std::vector<std::string> *output) {
  const base::ListValue *list = value.FindList(key);
  if (!list || list->size() > maximum) {
    return false;
  }
  for (const base::Value &entry : *list) {
    if (!entry.is_string() || entry.GetString().empty() ||
        entry.GetString().size() > kMaxSceneReferenceLength) {
      return false;
    }
    output->push_back(entry.GetString());
  }
  return true;
}

base::DictValue SceneToValue(const SceneDefinition &scene) {
  base::DictValue value;
  value.Set("schema_version", scene.schema_version);
  value.Set("id", scene.id);
  value.Set("name", scene.name);
  value.Set("workspace_id", scene.workspace_id);
  value.Set("theme_id", scene.theme_id);
  value.Set("site_layer_ids", StringListToValue(scene.site_layer_ids));
  value.Set("routing_rule_ids", StringListToValue(scene.routing_rule_ids));
  value.Set("workflow_shortcut_ids",
            StringListToValue(scene.workflow_shortcut_ids));
  base::DictValue lifecycle;
  lifecycle.Set("archive_temporary_tabs",
                scene.lifecycle.archive_temporary_tabs);
  lifecycle.Set("idle_archive_minutes", scene.lifecycle.idle_archive_minutes);
  lifecycle.Set("restore_on_activation", scene.lifecycle.restore_on_activation);
  value.Set("lifecycle", std::move(lifecycle));
  base::DictValue assistant;
  assistant.Set("allow_network", scene.assistant.allow_network);
  assistant.Set("allow_cloud_models", scene.assistant.allow_cloud_models);
  assistant.Set("max_sensitivity",
                DataSensitivityToWire(scene.assistant.max_sensitivity));
  assistant.Set("default_connectors",
                StringListToValue(scene.assistant.default_connectors));
  value.Set("assistant", std::move(assistant));
  value.Set("prefer_compact", scene.prefer_compact);
  return value;
}

std::optional<SceneDefinition> SceneFromValue(const base::Value &entry) {
  const base::DictValue *value = entry.GetIfDict();
  if (!value ||
      value->FindInt("schema_version").value_or(0) != kSceneSchemaVersion) {
    return std::nullopt;
  }
  const std::string *id = value->FindString("id");
  const std::string *name = value->FindString("name");
  const std::string *workspace = value->FindString("workspace_id");
  const std::string *theme = value->FindString("theme_id");
  const base::DictValue *lifecycle = value->FindDict("lifecycle");
  const base::DictValue *assistant = value->FindDict("assistant");
  if (!id || !name || !workspace || !theme || !lifecycle || !assistant) {
    return std::nullopt;
  }
  SceneDefinition scene;
  scene.id = *id;
  scene.name = *name;
  scene.workspace_id = *workspace;
  scene.theme_id = *theme;
  if (!ReadStringList(*value, "site_layer_ids", kMaxSceneSiteLayers,
                      &scene.site_layer_ids) ||
      !ReadStringList(*value, "routing_rule_ids", kMaxSceneRoutingRules,
                      &scene.routing_rule_ids) ||
      !ReadStringList(*value, "workflow_shortcut_ids",
                      kMaxSceneWorkflowShortcuts,
                      &scene.workflow_shortcut_ids) ||
      !ReadStringList(*assistant, "default_connectors", kMaxSceneContextTools,
                      &scene.assistant.default_connectors)) {
    return std::nullopt;
  }
  scene.lifecycle.archive_temporary_tabs =
      lifecycle->FindBool("archive_temporary_tabs").value_or(true);
  scene.lifecycle.idle_archive_minutes =
      lifecycle->FindInt("idle_archive_minutes").value_or(0);
  scene.lifecycle.restore_on_activation =
      lifecycle->FindBool("restore_on_activation").value_or(true);
  scene.assistant.allow_network =
      assistant->FindBool("allow_network").value_or(false);
  scene.assistant.allow_cloud_models =
      assistant->FindBool("allow_cloud_models").value_or(false);
  const std::string *sensitivity = assistant->FindString("max_sensitivity");
  if (!sensitivity || !DataSensitivityFromWire(
                          *sensitivity, &scene.assistant.max_sensitivity)) {
    return std::nullopt;
  }
  scene.prefer_compact = value->FindBool("prefer_compact").value_or(false);
  return scene;
}

} // namespace

SceneRegistry::SceneRegistry(SceneResolvers resolvers)
    : resolvers_(std::move(resolvers)) {}

SceneRegistry::~SceneRegistry() = default;

SceneStatusResult SceneRegistry::Validate(const SceneDefinition &scene) const {
  if (scene.schema_version != kSceneSchemaVersion) {
    return base::unexpected(SceneError::kUnsupportedSchema);
  }
  if (!ValidSlug(scene.id, kMaxSceneNameLength)) {
    return base::unexpected(SceneError::kInvalidId);
  }
  if (scene.name.empty() || scene.name.size() > kMaxSceneNameLength) {
    return base::unexpected(SceneError::kInvalidName);
  }
  if (scene.workspace_id.empty() ||
      scene.workspace_id.size() > kMaxSceneReferenceLength) {
    return base::unexpected(SceneError::kMissingWorkspace);
  }
  if (scene.theme_id.size() > kMaxSceneReferenceLength) {
    return base::unexpected(SceneError::kTooManySceneItems);
  }
  if (scene.site_layer_ids.size() > kMaxSceneSiteLayers ||
      scene.routing_rule_ids.size() > kMaxSceneRoutingRules ||
      scene.workflow_shortcut_ids.size() > kMaxSceneWorkflowShortcuts ||
      scene.assistant.default_connectors.size() > kMaxSceneContextTools) {
    return base::unexpected(SceneError::kTooManySceneItems);
  }
  if (!AllBoundedReferences(scene.site_layer_ids) ||
      !AllBoundedReferences(scene.routing_rule_ids) ||
      !AllBoundedReferences(scene.workflow_shortcut_ids) ||
      !AllBoundedReferences(scene.assistant.default_connectors)) {
    return base::unexpected(SceneError::kTooManySceneItems);
  }
  if (HasDuplicates(scene.site_layer_ids) ||
      HasDuplicates(scene.routing_rule_ids) ||
      HasDuplicates(scene.workflow_shortcut_ids)) {
    return base::unexpected(SceneError::kDuplicateReference);
  }
  if (scene.lifecycle.idle_archive_minutes < 1 ||
      scene.lifecycle.idle_archive_minutes > 10080) {
    return base::unexpected(SceneError::kInvalidLifecyclePolicy);
  }
  if (resolvers_.workspace_exists &&
      !resolvers_.workspace_exists.Run(scene.workspace_id)) {
    return base::unexpected(SceneError::kUnknownWorkspace);
  }
  if (!scene.theme_id.empty() && resolvers_.theme_exists &&
      !resolvers_.theme_exists.Run(scene.theme_id)) {
    return base::unexpected(SceneError::kUnknownTheme);
  }
  if (resolvers_.site_layer_exists) {
    for (const std::string &layer_id : scene.site_layer_ids) {
      if (!resolvers_.site_layer_exists.Run(layer_id)) {
        return base::unexpected(SceneError::kUnknownSiteLayer);
      }
    }
  }
  if (resolvers_.routing_rule_exists) {
    for (const std::string &rule_id : scene.routing_rule_ids) {
      if (!resolvers_.routing_rule_exists.Run(rule_id)) {
        return base::unexpected(SceneError::kUnknownRoutingRule);
      }
    }
  }
  if (resolvers_.workflow_exists) {
    for (const std::string &workflow_id : scene.workflow_shortcut_ids) {
      if (!resolvers_.workflow_exists.Run(workflow_id)) {
        return base::unexpected(SceneError::kUnknownWorkflow);
      }
    }
  }
  return base::ok();
}

SceneStatusResult SceneRegistry::Upsert(SceneDefinition scene) {
  if (auto valid = Validate(scene); !valid.has_value()) {
    return valid;
  }
  const bool exists = scenes_.find(scene.id) != scenes_.end();
  if (!exists && scenes_.size() >= kMaxScenes) {
    return base::unexpected(SceneError::kLimitExceeded);
  }
  const std::string id = scene.id;
  scenes_[id] = std::move(scene);
  return base::ok();
}

SceneStatusResult SceneRegistry::Remove(const std::string &scene_id) {
  if (scenes_.erase(scene_id) == 0) {
    return base::unexpected(SceneError::kUnknownScene);
  }
  return base::ok();
}

const SceneDefinition *SceneRegistry::Find(const std::string &scene_id) const {
  auto it = scenes_.find(scene_id);
  return it == scenes_.end() ? nullptr : &it->second;
}

std::vector<const SceneDefinition *> SceneRegistry::List() const {
  std::vector<const SceneDefinition *> result;
  for (const auto &[id, scene] : scenes_) {
    result.push_back(&scene);
  }
  return result;
}

void SceneRegistry::SetResolvers(SceneResolvers resolvers) {
  resolvers_ = std::move(resolvers);
}

base::DictValue SceneRegistry::TakePersistedState() const {
  base::DictValue state;
  state.Set("schema_version", kSceneRegistrySchemaVersion);
  base::ListValue scenes;
  for (const auto &[id, scene] : scenes_) {
    scenes.Append(SceneToValue(scene));
  }
  state.Set("scenes", std::move(scenes));
  return state;
}

void SceneRegistry::RestorePersistedState(const base::DictValue &state) {
  if (state.FindInt("schema_version").value_or(0) !=
      kSceneRegistrySchemaVersion) {
    return;
  }
  const base::ListValue *scenes = state.FindList("scenes");
  if (!scenes) {
    return;
  }
  SceneRegistry restored(resolvers_);
  for (const base::Value &entry : *scenes) {
    if (restored.size() >= kMaxScenes) {
      break;
    }
    std::optional<SceneDefinition> scene = SceneFromValue(entry);
    if (scene.has_value()) {
      std::ignore = restored.Upsert(std::move(scene.value()));
    }
  }
  scenes_ = std::move(restored.scenes_);
}

size_t SceneRegistry::PruneInvalidEntries() {
  size_t removed = 0;
  for (auto it = scenes_.begin(); it != scenes_.end();) {
    if (Validate(it->second).has_value()) {
      ++it;
      continue;
    }
    it = scenes_.erase(it);
    ++removed;
  }
  return removed;
}

SceneResult<std::vector<SceneActivationStep>>
SceneRegistry::BuildActivationPlan(const std::string &scene_id) const {
  const SceneDefinition *scene = Find(scene_id);
  if (!scene) {
    return base::unexpected(SceneError::kUnknownScene);
  }
  // Re-validate at activation time: referenced entities may have been removed
  // since the Scene was stored.
  if (auto valid = Validate(*scene); !valid.has_value()) {
    return base::unexpected(valid.error());
  }
  std::vector<SceneActivationStep> plan;
  plan.push_back(
      {SceneActivationStep::Kind::kSwitchWorkspace, scene->workspace_id});
  if (!scene->theme_id.empty()) {
    plan.push_back({SceneActivationStep::Kind::kApplyTheme, scene->theme_id});
  }
  for (const std::string &layer_id : scene->site_layer_ids) {
    plan.push_back({SceneActivationStep::Kind::kEnableSiteLayer, layer_id});
  }
  for (const std::string &rule_id : scene->routing_rule_ids) {
    plan.push_back({SceneActivationStep::Kind::kInstallRoutingRule, rule_id});
  }
  plan.push_back(
      {SceneActivationStep::Kind::kApplyLifecyclePolicy, std::string()});
  plan.push_back(
      {SceneActivationStep::Kind::kApplyAssistantDefaults, std::string()});
  plan.push_back(
      {SceneActivationStep::Kind::kSetCompactPreference, std::string()});
  return plan;
}

const char *SceneErrorToString(SceneError error) {
  switch (error) {
  case SceneError::kInvalidName:
    return "invalid_name";
  case SceneError::kInvalidId:
    return "invalid_id";
  case SceneError::kMissingWorkspace:
    return "missing_workspace";
  case SceneError::kTooManySceneItems:
    return "too_many_scene_items";
  case SceneError::kDuplicateReference:
    return "duplicate_reference";
  case SceneError::kUnknownScene:
    return "unknown_scene";
  case SceneError::kUnknownWorkspace:
    return "unknown_workspace";
  case SceneError::kUnknownTheme:
    return "unknown_theme";
  case SceneError::kUnknownSiteLayer:
    return "unknown_site_layer";
  case SceneError::kUnknownRoutingRule:
    return "unknown_routing_rule";
  case SceneError::kUnknownWorkflow:
    return "unknown_workflow";
  case SceneError::kInvalidLifecyclePolicy:
    return "invalid_lifecycle_policy";
  case SceneError::kUnsupportedSchema:
    return "unsupported_schema";
  case SceneError::kLimitExceeded:
    return "limit_exceeded";
  case SceneError::kInUse:
    return "in_use";
  case SceneError::kActivationFailed:
    return "activation_failed";
  }
  return "unknown_scene";
}

} // namespace seoul
