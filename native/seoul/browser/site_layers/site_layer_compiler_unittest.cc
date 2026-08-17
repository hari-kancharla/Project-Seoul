// Project Seoul Site Layers.
// Unit tests for selector safety, origin validation, compilation, and
// injection rejection.

#include "seoul/browser/site_layers/site_layer_compiler.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

SiteAdjustment HideAd() {
  SiteAdjustment adjustment;
  adjustment.kind = SiteAdjustmentKind::kHide;
  adjustment.selectors = {".ad-banner", "#promo"};
  return adjustment;
}

SiteLayer ReadableLayer() {
  SiteLayer layer;
  layer.id = "layer-1";
  layer.name = "Clean docs";
  layer.origin_pattern = "https://docs.example.com";
  layer.adjustments.push_back(HideAd());
  SiteAdjustment reading;
  reading.kind = SiteAdjustmentKind::kReadingMode;
  layer.adjustments.push_back(reading);
  return layer;
}

TEST(SiteLayerSelectorTest, AcceptsSafeSelectors) {
  EXPECT_TRUE(IsSafeSelector(".ad-banner"));
  EXPECT_TRUE(IsSafeSelector("#main-content"));
  EXPECT_TRUE(IsSafeSelector("article p"));
  EXPECT_TRUE(IsSafeSelector("nav > ul li"));
  EXPECT_TRUE(IsSafeSelector("div.class1.class2"));
  EXPECT_TRUE(IsSafeSelector("[data-role]"));
  EXPECT_TRUE(IsSafeSelector("main > section:nth-of-type(2) > article"));
}

TEST(SiteLayerSelectorTest, RejectsInjectionAttempts) {
  EXPECT_FALSE(IsSafeSelector("div { } body"));       // brace escape
  EXPECT_FALSE(IsSafeSelector("a; background:red"));  // semicolon escape
  EXPECT_FALSE(IsSafeSelector("a[href=\"x\"]"));      // quotes
  EXPECT_FALSE(IsSafeSelector("div/*comment*/"));     // comment
  EXPECT_FALSE(IsSafeSelector("@media screen"));      // at-rule
  EXPECT_FALSE(IsSafeSelector("a:hover(url(x))"));    // url()/parens
  EXPECT_FALSE(IsSafeSelector("li:nth-child(2)"));    // Zap-only pseudo subset
  EXPECT_FALSE(IsSafeSelector("li:nth-of-type(0)"));  // indices start at one
  EXPECT_FALSE(IsSafeSelector("li:nth-of-type(2n)")); // expressions forbidden
  EXPECT_FALSE(IsSafeSelector("</style><script>"));   // markup break-out
  EXPECT_FALSE(IsSafeSelector("*"));                  // no identifier
  EXPECT_FALSE(IsSafeSelector(""));
}

TEST(SiteLayerOriginTest, ValidatesOriginPatterns) {
  EXPECT_TRUE(IsValidOriginPattern("https://example.com"));
  EXPECT_TRUE(IsValidOriginPattern("https://sub.example.com:8443"));
  EXPECT_TRUE(IsValidOriginPattern("http://localhost:3000"));
  EXPECT_TRUE(IsValidOriginPattern("http://[::1]:3000"));
  EXPECT_TRUE(IsValidOriginPattern("https://EXAMPLE.com:443"));
  EXPECT_TRUE(IsValidOriginPattern("*.example.com"));
  EXPECT_FALSE(IsValidOriginPattern("ftp://example.com"));    // not web
  EXPECT_FALSE(IsValidOriginPattern("example.com"));          // no scheme
  EXPECT_FALSE(IsValidOriginPattern("https://exam ple.com")); // space
  EXPECT_FALSE(IsValidOriginPattern("https://"));             // empty host
  EXPECT_FALSE(IsValidOriginPattern("https://a..b.com"));     // double dot
  EXPECT_FALSE(IsValidOriginPattern("https://host:0"));       // bad port
  EXPECT_FALSE(IsValidOriginPattern("https://example.com/path"));
  EXPECT_FALSE(IsValidOriginPattern("https://user@example.com"));
}

TEST(SiteLayerCompilerTest, CompilesDeterministicScopedCss) {
  auto css = CompileSiteLayer(ReadableLayer());
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find(".ad-banner, #promo { display: none !important; }"),
            std::string::npos);
  EXPECT_NE(css->find("max-width: 720px"), std::string::npos);
  // No braces or semicolons leaked from selectors; the CSS is well-formed.
  EXPECT_EQ(css->find("<script>"), std::string::npos);
}

TEST(SiteLayerCompilerTest, RejectsUnsafeSelectorInAdjustment) {
  SiteLayer layer = ReadableLayer();
  layer.adjustments[0].selectors = {"div { color:red } x"};
  EXPECT_EQ(CompileSiteLayer(layer).error(), SiteLayerError::kUnsafeSelector);
}

TEST(SiteLayerCompilerTest, RejectsInvalidLayerId) {
  SiteLayer layer = ReadableLayer();
  layer.id = "Bad Id";
  EXPECT_EQ(CompileSiteLayer(layer).error(), SiteLayerError::kInvalidId);
}

TEST(SiteLayerCompilerTest, EnforcesSelectorScopingRules) {
  SiteLayer layer = ReadableLayer();
  // Reading mode is document-scoped; giving it selectors is rejected.
  layer.adjustments[1].selectors = {".content"};
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kSelectorNotAllowed);

  layer = ReadableLayer();
  // Hide requires selectors.
  layer.adjustments[0].selectors.clear();
  EXPECT_EQ(CompileSiteLayer(layer).error(), SiteLayerError::kSelectorRequired);
}

TEST(SiteLayerCompilerTest, ValidatesColorFontAndNumericRanges) {
  SiteLayer layer = ReadableLayer();
  SiteAdjustment recolor;
  recolor.kind = SiteAdjustmentKind::kAccentColor;
  recolor.selectors = {"a"};
  recolor.color_value = "not-a-color";
  layer.adjustments = {recolor};
  EXPECT_EQ(CompileSiteLayer(layer).error(), SiteLayerError::kInvalidColor);

  recolor.color_value = "#1a2b3c";
  layer.adjustments = {recolor};
  EXPECT_TRUE(CompileSiteLayer(layer).has_value());

  SiteAdjustment font;
  font.kind = SiteAdjustmentKind::kFontFamily;
  font.selectors = {"body"};
  font.font_family = "Comic Sans; }"; // injection attempt
  layer.adjustments = {font};
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kInvalidFontFamily);

  SiteAdjustment scale;
  scale.kind = SiteAdjustmentKind::kFontSizeScale;
  scale.selectors = {"p"};
  scale.numeric_value = 9.0; // out of [0.5, 2.0]
  layer.adjustments = {scale};
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kInvalidNumericValue);
}

TEST(SiteLayerCompilerTest, CompilesTintAndDocumentFont) {
  SiteLayer layer = ReadableLayer();
  SiteAdjustment tint;
  tint.kind = SiteAdjustmentKind::kTintColor;
  tint.color_value = "#7a5cff";
  tint.numeric_value = 0.3;
  SiteAdjustment font;
  font.kind = SiteAdjustmentKind::kFontFamily;
  font.font_family = "Atkinson Hyperlegible";
  layer.adjustments = {tint, font};

  auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find("background: #7a5cff !important"), std::string::npos);
  EXPECT_NE(css->find("opacity: 0.3 !important"), std::string::npos);
  EXPECT_NE(css->find("mix-blend-mode: color !important"), std::string::npos);
  EXPECT_NE(css->find("html, body, body *, input, button, textarea, select"),
            std::string::npos);
  EXPECT_NE(css->find("font-family: Atkinson Hyperlegible"), std::string::npos);

  tint.numeric_value = 0.049;
  layer.adjustments = {tint};
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kInvalidNumericValue);
  tint.numeric_value = 0.751;
  layer.adjustments = {tint};
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kInvalidNumericValue);
}

TEST(SiteLayerCompilerTest, AllowsEmptyDraftAndPersistsAutomaticDarkMode) {
  SiteLayer layer = ReadableLayer();
  layer.adjustments.clear();
  auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_TRUE(css->empty());

  SiteAdjustment automatic_dark;
  automatic_dark.kind = SiteAdjustmentKind::kAutomaticDarkMode;
  layer.adjustments.push_back(automatic_dark);
  css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_TRUE(css->empty());

  base::DictValue serialized = SiteLayerToValue(layer);
  auto parsed = SiteLayerFromValue(base::Value(serialized.Clone()));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), layer);
}

TEST(SiteLayerCompilerTest, RoundTripsThroughJson) {
  SiteLayer layer = ReadableLayer();
  SiteAdjustment density;
  density.kind = SiteAdjustmentKind::kDensity;
  density.density = DensityLevel::kCompact;
  layer.adjustments.push_back(density);
  SiteAdjustment recolor;
  recolor.kind = SiteAdjustmentKind::kTextColor;
  recolor.selectors = {"p"};
  recolor.color_value = "#222222";
  layer.adjustments.push_back(recolor);
  SiteAdjustment tint;
  tint.kind = SiteAdjustmentKind::kTintColor;
  tint.color_value = "#336699";
  tint.numeric_value = 0.2;
  layer.adjustments.push_back(tint);
  SiteAdjustment automatic_dark;
  automatic_dark.kind = SiteAdjustmentKind::kAutomaticDarkMode;
  layer.adjustments.push_back(automatic_dark);

  base::DictValue serialized = SiteLayerToValue(layer);
  auto parsed = SiteLayerFromValue(base::Value(serialized.Clone()));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), layer);
}

TEST(SiteLayerCompilerTest, ImportRejectsMaliciousLayer) {
  SiteLayer layer = ReadableLayer();
  base::DictValue serialized = SiteLayerToValue(layer);
  // Tamper with the serialized selector to inject a rule.
  base::ListValue *adjustments = serialized.FindList("adjustments");
  ASSERT_NE(adjustments, nullptr);
  base::ListValue *selectors =
      (*adjustments)[0].GetDict().FindList("selectors");
  ASSERT_NE(selectors, nullptr);
  selectors->clear();
  selectors->Append("a { } body[onload=alert(1)]");
  EXPECT_EQ(SiteLayerFromValue(base::Value(std::move(serialized))).error(),
            SiteLayerError::kUnsafeSelector);
}

// Arc's three "Advanced color controls" are independent controls but one CSS
// declaration. `filter` does not accumulate across rules - a second
// declaration replaces the first - so emitting them separately would silently
// apply only the last slider the user touched.
TEST(SiteLayerCompilerTest, ColorSlidersCompileIntoOneFilterDeclaration) {
  SiteLayer layer;
  layer.id = "boost-filters";
  layer.name = "Filters";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  const std::pair<SiteAdjustmentKind, double> sliders[] = {
      {SiteAdjustmentKind::kContrastLevel, 1.2},
      {SiteAdjustmentKind::kBrightnessLevel, 0.9},
      {SiteAdjustmentKind::kSaturationLevel, 1.4}};
  for (const auto &[kind, value] : sliders) {
    SiteAdjustment adjustment;
    adjustment.kind = kind;
    adjustment.numeric_value = value;
    layer.adjustments.push_back(adjustment);
  }

  const auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_EQ(css->find("filter:"), css->rfind("filter:"))
      << "exactly one filter declaration, or only the last slider applies";
  EXPECT_NE(css->find("contrast(1.2)"), std::string::npos);
  EXPECT_NE(css->find("brightness(0.9)"), std::string::npos);
  EXPECT_NE(css->find("saturate(1.4)"), std::string::npos);
}

// A slider the user never touched stays at 1.0: the page exactly as authored.
TEST(SiteLayerCompilerTest, UntouchedColorSlidersStayNeutral) {
  SiteLayer layer;
  layer.id = "boost-one-slider";
  layer.name = "One slider";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  SiteAdjustment brightness;
  brightness.kind = SiteAdjustmentKind::kBrightnessLevel;
  brightness.numeric_value = 0.8;
  layer.adjustments.push_back(brightness);

  const auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find("contrast(1)"), std::string::npos);
  EXPECT_NE(css->find("brightness(0.8)"), std::string::npos);
  EXPECT_NE(css->find("saturate(1)"), std::string::npos);
}

// Arc's "Case" applies to all text on the page, and survives persistence.
TEST(SiteLayerCompilerTest, TextCaseCompilesAndRoundTrips) {
  SiteLayer layer;
  layer.id = "boost-case";
  layer.name = "Case";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  SiteAdjustment adjustment;
  adjustment.kind = SiteAdjustmentKind::kTextCase;
  adjustment.text_case = TextCase::kUpper;
  layer.adjustments.push_back(adjustment);

  const auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  EXPECT_NE(css->find("text-transform: uppercase"), std::string::npos);

  const auto restored = SiteLayerFromValue(base::Value(SiteLayerToValue(layer)));
  ASSERT_TRUE(restored.has_value());
  ASSERT_EQ(restored->adjustments.size(), 1u);
  EXPECT_EQ(restored->adjustments.front().text_case, TextCase::kUpper);
}

// A slider outside the accepted band is refused rather than clamped, so a
// stored layer can never render a page unreadable.
TEST(SiteLayerCompilerTest, OutOfRangeColorSliderIsRejected) {
  SiteLayer layer;
  layer.id = "boost-bad";
  layer.name = "Bad";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  SiteAdjustment adjustment;
  adjustment.kind = SiteAdjustmentKind::kContrastLevel;
  adjustment.numeric_value = 9.0;
  layer.adjustments.push_back(adjustment);

  EXPECT_FALSE(CompileSiteLayer(layer).has_value());
}

// Arc's Code editor. Author CSS is appended after the compiled controls so it
// wins on equal specificity - overriding what the controls produced is the
// reason to drop to code at all.
TEST(SiteLayerCompilerTest, AuthorCssIsAppendedAfterTheTypedAdjustments) {
  SiteLayer layer;
  layer.id = "boost-code";
  layer.name = "Code";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  SiteAdjustment scale;
  scale.kind = SiteAdjustmentKind::kFontSizeScale;
  scale.selectors = {"p"};
  scale.numeric_value = 1.2;
  layer.adjustments.push_back(scale);
  layer.custom_css = ".sidebar { display: none; }";

  const auto css = CompileSiteLayer(layer);
  ASSERT_TRUE(css.has_value());
  const size_t typed = css->find("font-size");
  const size_t author = css->find(".sidebar");
  ASSERT_NE(typed, std::string::npos);
  ASSERT_NE(author, std::string::npos);
  EXPECT_LT(typed, author) << "author CSS must come last to win on ties";
}

// A Boost that is nothing but Code-editor content is a real Boost, not an
// empty layer.
TEST(SiteLayerCompilerTest, CodeOnlyLayerRoundTrips) {
  SiteLayer layer;
  layer.id = "boost-code-only";
  layer.name = "Code only";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  layer.custom_css = "body { color: red; }";
  layer.custom_javascript = "console.log('hi');";

  const auto restored = SiteLayerFromValue(base::Value(SiteLayerToValue(layer)));
  ASSERT_TRUE(restored.has_value())
      << "a layer carrying only code must survive persistence";
  EXPECT_EQ(restored->custom_css, layer.custom_css);
  EXPECT_EQ(restored->custom_javascript, layer.custom_javascript);
}

// The Code editor is bounded, so a layer cannot become an unbounded blob in
// the profile.
TEST(SiteLayerCompilerTest, OversizeAuthorCodeIsRejected) {
  SiteLayer layer;
  layer.id = "boost-huge";
  layer.name = "Huge";
  layer.origin_pattern = "https://example.test";
  layer.enabled = true;
  layer.custom_css = std::string(kMaxCustomCssLength + 1, 'x');
  EXPECT_EQ(CompileSiteLayer(layer).error(),
            SiteLayerError::kCustomCodeTooLong);
}

} // namespace
} // namespace seoul
