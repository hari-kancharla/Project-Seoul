// Project Seoul Scenes.
// Unit tests for Scene validation, reference resolution, and deterministic
// activation planning.

#include "seoul/browser/scenes/scene_registry.h"

#include <set>

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class SceneRegistryTest : public testing::Test {
protected:
  SceneRegistryTest() {
    workspaces_ = {"ws-research", "ws-shopping"};
    themes_ = {"theme-focus"};
    layers_ = {"layer-docs", "layer-news"};
    routing_rules_ = {"rule-1"};
    workflows_ = {"workflow-1"};
  }

  SceneResolvers Resolvers() {
    SceneResolvers resolvers;
    resolvers.workspace_exists = base::BindLambdaForTesting(
        [this](const std::string &id) { return workspaces_.count(id) > 0; });
    resolvers.theme_exists = base::BindLambdaForTesting(
        [this](const std::string &id) { return themes_.count(id) > 0; });
    resolvers.site_layer_exists = base::BindLambdaForTesting(
        [this](const std::string &id) { return layers_.count(id) > 0; });
    resolvers.routing_rule_exists = base::BindLambdaForTesting(
        [this](const std::string &id) { return routing_rules_.count(id) > 0; });
    resolvers.workflow_exists = base::BindLambdaForTesting(
        [this](const std::string &id) { return workflows_.count(id) > 0; });
    return resolvers;
  }

  SceneDefinition ResearchScene() {
    SceneDefinition scene;
    scene.id = "research";
    scene.name = "Research";
    scene.workspace_id = "ws-research";
    scene.theme_id = "theme-focus";
    scene.site_layer_ids = {"layer-docs", "layer-news"};
    scene.routing_rule_ids = {"rule-1"};
    return scene;
  }

  std::set<std::string> workspaces_;
  std::set<std::string> themes_;
  std::set<std::string> layers_;
  std::set<std::string> routing_rules_;
  std::set<std::string> workflows_;
};

TEST_F(SceneRegistryTest, UpsertAndFind) {
  SceneRegistry registry(Resolvers());
  ASSERT_TRUE(registry.Upsert(ResearchScene()).has_value());
  const SceneDefinition *found = registry.Find("research");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, "Research");
  EXPECT_EQ(registry.size(), 1u);
}

TEST_F(SceneRegistryTest, PrunesEntriesWhoseReferencesDisappear) {
  SceneRegistry registry(Resolvers());
  SceneDefinition scene = ResearchScene();
  scene.workflow_shortcut_ids = {"workflow-1"};
  ASSERT_TRUE(registry.Upsert(std::move(scene)).has_value());
  ASSERT_EQ(registry.size(), 1u);

  workflows_.clear();
  EXPECT_EQ(registry.PruneInvalidEntries(), 1u);
  EXPECT_EQ(registry.size(), 0u);
  EXPECT_EQ(registry.PruneInvalidEntries(), 0u);
}

TEST_F(SceneRegistryTest, RejectsUnknownReferences) {
  SceneRegistry registry(Resolvers());
  SceneDefinition bad_workspace = ResearchScene();
  bad_workspace.workspace_id = "ws-missing";
  EXPECT_EQ(registry.Upsert(bad_workspace).error(),
            SceneError::kUnknownWorkspace);

  SceneDefinition bad_theme = ResearchScene();
  bad_theme.theme_id = "theme-missing";
  EXPECT_EQ(registry.Upsert(bad_theme).error(), SceneError::kUnknownTheme);

  SceneDefinition bad_layer = ResearchScene();
  bad_layer.site_layer_ids = {"layer-docs", "layer-missing"};
  EXPECT_EQ(registry.Upsert(bad_layer).error(), SceneError::kUnknownSiteLayer);

  SceneDefinition bad_rule = ResearchScene();
  bad_rule.routing_rule_ids = {"rule-missing"};
  EXPECT_EQ(registry.Upsert(bad_rule).error(), SceneError::kUnknownRoutingRule);

  SceneDefinition bad_workflow = ResearchScene();
  bad_workflow.workflow_shortcut_ids = {"workflow-missing"};
  EXPECT_EQ(registry.Upsert(bad_workflow).error(),
            SceneError::kUnknownWorkflow);
}

TEST_F(SceneRegistryTest, RejectsIdentityAndDuplicateDefects) {
  SceneRegistry registry(Resolvers());
  SceneDefinition no_name = ResearchScene();
  no_name.name.clear();
  EXPECT_EQ(registry.Upsert(no_name).error(), SceneError::kInvalidName);

  SceneDefinition no_workspace = ResearchScene();
  no_workspace.workspace_id.clear();
  EXPECT_EQ(registry.Upsert(no_workspace).error(),
            SceneError::kMissingWorkspace);

  SceneDefinition duplicate_layers = ResearchScene();
  duplicate_layers.site_layer_ids = {"layer-docs", "layer-docs"};
  EXPECT_EQ(registry.Upsert(duplicate_layers).error(),
            SceneError::kDuplicateReference);

  SceneDefinition bad_id = ResearchScene();
  bad_id.id = "Has Spaces";
  EXPECT_EQ(registry.Upsert(bad_id).error(), SceneError::kInvalidId);
}

TEST_F(SceneRegistryTest, ActivationPlanIsDeterministicAndOrdered) {
  SceneRegistry registry(Resolvers());
  ASSERT_TRUE(registry.Upsert(ResearchScene()).has_value());
  auto plan = registry.BuildActivationPlan("research");
  ASSERT_TRUE(plan.has_value());

  ASSERT_GE(plan->size(), 6u);
  EXPECT_EQ(plan->at(0).kind, SceneActivationStep::Kind::kSwitchWorkspace);
  EXPECT_EQ(plan->at(0).target_id, "ws-research");
  EXPECT_EQ(plan->at(1).kind, SceneActivationStep::Kind::kApplyTheme);
  EXPECT_EQ(plan->at(2).kind, SceneActivationStep::Kind::kEnableSiteLayer);
  EXPECT_EQ(plan->at(2).target_id, "layer-docs");
  EXPECT_EQ(plan->at(3).kind, SceneActivationStep::Kind::kEnableSiteLayer);
  EXPECT_EQ(plan->at(3).target_id, "layer-news");
  EXPECT_EQ(plan->at(4).kind, SceneActivationStep::Kind::kInstallRoutingRule);

  // Tail always includes lifecycle, assistant, compact.
  bool has_lifecycle = false;
  bool has_assistant = false;
  bool has_compact = false;
  for (const SceneActivationStep &step : plan.value()) {
    if (step.kind == SceneActivationStep::Kind::kApplyLifecyclePolicy) {
      has_lifecycle = true;
    }
    if (step.kind == SceneActivationStep::Kind::kApplyAssistantDefaults) {
      has_assistant = true;
    }
    if (step.kind == SceneActivationStep::Kind::kSetCompactPreference) {
      has_compact = true;
    }
  }
  EXPECT_TRUE(has_lifecycle);
  EXPECT_TRUE(has_assistant);
  EXPECT_TRUE(has_compact);
}

TEST_F(SceneRegistryTest, ActivationRevalidatesAgainstCurrentState) {
  SceneRegistry registry(Resolvers());
  ASSERT_TRUE(registry.Upsert(ResearchScene()).has_value());
  // A referenced layer is removed after the Scene was stored.
  layers_.erase("layer-news");
  EXPECT_EQ(registry.BuildActivationPlan("research").error(),
            SceneError::kUnknownSiteLayer);
}

TEST_F(SceneRegistryTest, ThemelessSceneOmitsThemeStep) {
  SceneRegistry registry(Resolvers());
  SceneDefinition scene = ResearchScene();
  scene.theme_id.clear();
  scene.site_layer_ids.clear();
  scene.routing_rule_ids.clear();
  ASSERT_TRUE(registry.Upsert(scene).has_value());
  auto plan = registry.BuildActivationPlan("research");
  ASSERT_TRUE(plan.has_value());
  for (const SceneActivationStep &step : plan.value()) {
    EXPECT_NE(step.kind, SceneActivationStep::Kind::kApplyTheme);
    EXPECT_NE(step.kind, SceneActivationStep::Kind::kEnableSiteLayer);
  }
}

TEST_F(SceneRegistryTest, RejectsUnboundedReferences) {
  SceneRegistry registry(Resolvers());
  SceneDefinition scene = ResearchScene();
  scene.routing_rule_ids.assign(kMaxSceneRoutingRules + 1, "rule");
  EXPECT_EQ(registry.Upsert(scene).error(), SceneError::kTooManySceneItems);

  scene = ResearchScene();
  scene.routing_rule_ids = {std::string(kMaxSceneReferenceLength + 1, 'x')};
  EXPECT_EQ(registry.Upsert(scene).error(), SceneError::kTooManySceneItems);
}

TEST_F(SceneRegistryTest, PersistenceRoundTripsAndSkipsInvalidEntries) {
  SceneRegistry registry(Resolvers());
  SceneDefinition scene = ResearchScene();
  scene.workflow_shortcut_ids = {"workflow-1"};
  scene.lifecycle.archive_temporary_tabs = false;
  scene.lifecycle.idle_archive_minutes = 30;
  scene.lifecycle.restore_on_activation = false;
  scene.assistant.allow_network = true;
  scene.assistant.allow_cloud_models = true;
  scene.assistant.max_sensitivity = DataSensitivity::kPersonal;
  scene.assistant.default_connectors = {"connector-1"};
  scene.prefer_compact = true;
  ASSERT_TRUE(registry.Upsert(scene).has_value());

  base::DictValue state = registry.TakePersistedState();
  base::ListValue *scenes = state.FindList("scenes");
  ASSERT_NE(scenes, nullptr);
  scenes->Append(base::DictValue().Set("schema_version", 1));

  SceneRegistry restored(Resolvers());
  restored.RestorePersistedState(state);
  ASSERT_EQ(restored.size(), 1u);
  const SceneDefinition *found = restored.Find("research");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, scene);

  base::DictValue wrong_schema;
  wrong_schema.Set("schema_version", 99);
  wrong_schema.Set("scenes", base::ListValue());
  restored.RestorePersistedState(wrong_schema);
  EXPECT_EQ(restored.size(), 1u);
}

TEST_F(SceneRegistryTest, RestoreRejectsReferencesRemovedSinceSave) {
  SceneRegistry registry(Resolvers());
  ASSERT_TRUE(registry.Upsert(ResearchScene()).has_value());
  base::DictValue state = registry.TakePersistedState();
  themes_.erase("theme-focus");

  SceneRegistry restored(Resolvers());
  restored.RestorePersistedState(state);
  EXPECT_EQ(restored.size(), 0u);
}

} // namespace
} // namespace seoul
