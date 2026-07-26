// Project Seoul Site Layers.
// Unit tests for validated layer storage and page-origin resolution.

#include "seoul/browser/site_layers/site_layer_registry.h"

#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

SiteAdjustment HideSelector(const std::string &selector) {
  SiteAdjustment adjustment;
  adjustment.kind = SiteAdjustmentKind::kHide;
  adjustment.selectors = {selector};
  return adjustment;
}

SiteLayer Layer(std::string id, std::string origin) {
  SiteLayer layer;
  layer.id = std::move(id);
  layer.name = "Layer";
  layer.origin_pattern = std::move(origin);
  layer.adjustments.push_back(HideSelector(".noise"));
  return layer;
}

} // namespace

TEST(SiteLayerRegistryTest, UpsertFindListAndRemove) {
  SiteLayerRegistry registry;
  ASSERT_TRUE(registry.Upsert(Layer("docs-clean", "https://docs.example.com"))
                  .has_value());
  EXPECT_TRUE(registry.Exists("docs-clean"));
  ASSERT_NE(registry.Find("docs-clean"), nullptr);
  EXPECT_EQ(registry.List().size(), 1u);

  ASSERT_TRUE(registry.Remove("docs-clean").has_value());
  EXPECT_FALSE(registry.Exists("docs-clean"));
  EXPECT_EQ(registry.Remove("docs-clean").error(),
            SiteLayerError::kUnknownLayer);
}

TEST(SiteLayerRegistryTest, RejectsInvalidLayerBeforeStorage) {
  SiteLayerRegistry registry;
  SiteLayer layer = Layer("Bad Id", "https://docs.example.com");
  EXPECT_EQ(registry.Upsert(layer).error(), SiteLayerError::kInvalidId);

  layer = Layer("docs-clean", "ftp://docs.example.com");
  EXPECT_EQ(registry.Upsert(layer).error(), SiteLayerError::kInvalidOrigin);

  layer = Layer("docs-clean", "https://docs.example.com");
  layer.adjustments[0].selectors = {"div { color:red }"};
  EXPECT_EQ(registry.Upsert(layer).error(), SiteLayerError::kUnsafeSelector);
  EXPECT_EQ(registry.size(), 0u);
}

TEST(SiteLayerRegistryTest, CompilesMatchingLayersForOriginAndScene) {
  SiteLayerRegistry registry;
  SiteLayer global = Layer("global-docs", "*.example.com");
  global.adjustments[0].selectors = {".global-noise"};
  ASSERT_TRUE(registry.Upsert(global).has_value());

  SiteLayer scene = Layer("focus-docs", "https://docs.example.com");
  scene.scene_scope = "focus";
  scene.adjustments[0].selectors = {".focus-noise"};
  ASSERT_TRUE(registry.Upsert(scene).has_value());

  SiteLayer disabled = Layer("disabled-docs", "https://docs.example.com");
  disabled.enabled = false;
  disabled.adjustments[0].selectors = {".disabled-noise"};
  ASSERT_TRUE(registry.Upsert(disabled).has_value());

  auto css = registry.CompileForOrigin("https://docs.example.com", "focus");
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find(".global-noise"), std::string::npos);
  EXPECT_NE(css->find(".focus-noise"), std::string::npos);
  EXPECT_EQ(css->find(".disabled-noise"), std::string::npos);

  css = registry.CompileForOrigin("https://docs.example.com", "other");
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find(".global-noise"), std::string::npos);
  EXPECT_EQ(css->find(".focus-noise"), std::string::npos);
}

TEST(SiteLayerRegistryTest, FindsEnabledAdjustmentForOriginAndScene) {
  SiteLayerRegistry registry;
  SiteLayer global = Layer("global-docs", "*.example.com");
  SiteAdjustment automatic_dark;
  automatic_dark.kind = SiteAdjustmentKind::kAutomaticDarkMode;
  global.adjustments.push_back(automatic_dark);
  ASSERT_TRUE(registry.Upsert(global).has_value());

  SiteLayer scoped = Layer("focus-docs", "https://docs.example.com");
  scoped.scene_scope = "focus";
  SiteAdjustment tint;
  tint.kind = SiteAdjustmentKind::kTintColor;
  tint.color_value = "#123456";
  tint.numeric_value = 0.25;
  scoped.adjustments.push_back(tint);
  ASSERT_TRUE(registry.Upsert(scoped).has_value());

  EXPECT_TRUE(registry.HasEnabledAdjustmentForOrigin(
      "https://docs.example.com", "", SiteAdjustmentKind::kAutomaticDarkMode));
  EXPECT_FALSE(registry.HasEnabledAdjustmentForOrigin(
      "https://other.test", "", SiteAdjustmentKind::kAutomaticDarkMode));
  EXPECT_TRUE(registry.HasEnabledAdjustmentForOrigin(
      "https://docs.example.com", "focus", SiteAdjustmentKind::kTintColor));
  EXPECT_FALSE(registry.HasEnabledAdjustmentForOrigin(
      "https://docs.example.com", "other", SiteAdjustmentKind::kTintColor));

  global.enabled = false;
  ASSERT_TRUE(registry.Upsert(global).has_value());
  EXPECT_FALSE(registry.HasEnabledAdjustmentForOrigin(
      "https://docs.example.com", "", SiteAdjustmentKind::kAutomaticDarkMode));
}

TEST(SiteLayerRegistryTest, MatchesExactPortsAndWildcardHosts) {
  SiteLayer exact = Layer("local-docs", "https://docs.example.com:8443");
  EXPECT_TRUE(SiteLayerMatchesOrigin(exact, "https://docs.example.com:8443"));
  EXPECT_FALSE(SiteLayerMatchesOrigin(exact, "https://docs.example.com"));
  EXPECT_FALSE(SiteLayerMatchesOrigin(exact, "http://docs.example.com:8443"));

  SiteLayer local = Layer("local-http", "http://localhost:3000");
  EXPECT_TRUE(SiteLayerMatchesOrigin(local, "http://localhost:3000"));
  EXPECT_FALSE(SiteLayerMatchesOrigin(local, "https://localhost:3000"));
  SiteLayer local_ipv6 = Layer("local-ipv6", "http://[::1]:3000");
  EXPECT_TRUE(SiteLayerMatchesOrigin(local_ipv6, "http://[::1]:3000"));

  SiteLayer default_port = Layer("default-port", "https://example.com:443");
  EXPECT_TRUE(SiteLayerMatchesOrigin(default_port, "https://example.com"));

  SiteLayer wildcard = Layer("all-docs", "*.example.com");
  EXPECT_TRUE(SiteLayerMatchesOrigin(wildcard, "https://example.com"));
  EXPECT_TRUE(SiteLayerMatchesOrigin(wildcard, "https://docs.example.com"));
  EXPECT_TRUE(SiteLayerMatchesOrigin(wildcard, "http://docs.example.com"));
  EXPECT_FALSE(SiteLayerMatchesOrigin(wildcard, "https://badexample.com"));
}

TEST(SiteLayerRegistryTest, InvalidPageOriginFailsClosed) {
  SiteLayerRegistry registry;
  ASSERT_TRUE(
      registry.Upsert(Layer("docs-clean", "*.example.com")).has_value());
  EXPECT_EQ(registry.CompileForOrigin("chrome://settings", "").error(),
            SiteLayerError::kInvalidOrigin);
}

TEST(SiteLayerRegistryTest, PersistenceRoundTripsAndSkipsInvalidEntries) {
  SiteLayerRegistry registry;
  ASSERT_TRUE(
      registry.Upsert(Layer("docs-clean", "*.example.com")).has_value());
  base::DictValue state = registry.TakePersistedState();
  base::ListValue *layers = state.FindList("site_layers");
  ASSERT_NE(layers, nullptr);
  layers->Append(base::DictValue().Set("schema_version", 1));

  SiteLayerRegistry restored;
  restored.RestorePersistedState(state);
  ASSERT_EQ(restored.size(), 1u);
  ASSERT_NE(restored.Find("docs-clean"), nullptr);
  EXPECT_EQ(restored.Find("docs-clean")->origin_pattern, "*.example.com");

  base::DictValue wrong_schema;
  wrong_schema.Set("schema_version", 99);
  wrong_schema.Set("site_layers", base::ListValue());
  restored.RestorePersistedState(wrong_schema);
  EXPECT_EQ(restored.size(), 1u);
}

} // namespace seoul
