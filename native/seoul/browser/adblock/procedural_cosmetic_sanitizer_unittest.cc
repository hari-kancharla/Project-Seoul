// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/adblock/procedural_cosmetic_sanitizer.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

TEST(ProceduralCosmeticSanitizerTest, AcceptsFixedSupportedSchema) {
  const SanitizedProceduralActionSets result =
      SanitizeProceduralActionSets(
          {
              R"({"selector":[{"type":"css-selector","arg":".ad"},{"type":"has-text","arg":"Promoted"}]})",
              R"({"selector":[{"type":"css-selector","arg":".overlay"}],"action":{"type":"remove"}})",
              R"({"selector":[{"type":"css-selector","arg":"[onclick]"}],"action":{"type":"remove-attr","arg":"onclick"}})",
              R"({"selector":[{"type":"css-selector","arg":".sticky"}],"action":{"type":"remove-class","arg":"sticky"}})",
              R"({"selector":[{"type":"css-selector","arg":".article"},{"type":"min-text-length","arg":"100"},{"type":"upward","arg":"1"}]})",
          },
          {});

  EXPECT_EQ(5u, result.default_actions.size());
  EXPECT_TRUE(result.additional_actions.empty());
}

TEST(ProceduralCosmeticSanitizerTest,
     RejectsUnsupportedExecutableAndMalformedInputs) {
  const SanitizedProceduralActionSets result =
      SanitizeProceduralActionSets(
          {
              R"({"selector":[{"type":"css-selector","arg":".ad"},{"type":"has-text","arg":"/sponsor.*/i"}]})",
              R"({"selector":[{"type":"matches-css","arg":"display: block"}]})",
              R"({"selector":[{"type":"xpath","arg":"//div"}]})",
              R"({"selector":[{"type":"css-selector","arg":"body{display:none}"}]})",
              R"({"selector":[{"type":"css-selector","arg":".ad"}],"extra":true})",
              // Custom delimiter: the payload embeds `)"`, which would close a
              // plain R"(...)" literal in the middle of the string.
              R"json({"selector":[{"type":"css-selector","arg":".ad"}],"action":{"type":"remove-attr","arg":"onclick;alert(1)"}})json",
              R"({"selector":[]})",
              R"({"selector":"not-a-list"})",
              "not-json",
          },
          {});

  EXPECT_TRUE(result.default_actions.empty());
  EXPECT_TRUE(result.additional_actions.empty());
  EXPECT_TRUE(result.default_styled.empty());
  EXPECT_TRUE(result.additional_styled.empty());
}

// `:style()` in its plain form is a stylesheet rule wearing procedural
// clothing: one selector, one declaration block, nothing to evaluate against
// the DOM. It used to be dropped, which is what made hiding a consent notice
// leave the page scroll-locked - the rule that restores scrolling IS a style
// rule. It is now emitted as CSS rather than as procedural work.
TEST(ProceduralCosmeticSanitizerTest, AcceptsPlainStyleRulesAsStylesheetRules) {
  const SanitizedProceduralActionSets result = SanitizeProceduralActionSets(
      {
          R"({"selector":[{"type":"css-selector","arg":"html"}],"action":{"type":"style","arg":"overflow:auto!important"}})",
          // Duplicate of the first: one budget, deduplicated.
          R"({"selector":[{"type":"css-selector","arg":"html"}],"action":{"type":"style","arg":"overflow:auto!important"}})",
      },
      {
          R"({"selector":[{"type":"css-selector","arg":"body"}],"action":{"type":"style","arg":"position:static"}})",
      });

  ASSERT_EQ(1u, result.default_styled.size());
  EXPECT_EQ("html", result.default_styled[0].selector);
  EXPECT_EQ("overflow:auto!important", result.default_styled[0].declarations);
  ASSERT_EQ(1u, result.additional_styled.size());
  EXPECT_EQ("body", result.additional_styled[0].selector);
  // Nothing procedural was produced: these cost the renderer no DOM work.
  EXPECT_TRUE(result.default_actions.empty());
  EXPECT_TRUE(result.additional_actions.empty());
}

// The declarations are list-controlled CSS injected into the document, so the
// vocabulary is an allowlist of shapes rather than a hunt for known-bad ones.
// Rejecting every parenthesis removes url(), image-set(), attr(), var() and the
// legacy expression() channel in one rule that cannot fall behind new CSS.
TEST(ProceduralCosmeticSanitizerTest, RejectsStyleDeclarationsThatCouldEscape) {
  const SanitizedProceduralActionSets result = SanitizeProceduralActionSets(
      {
          // Custom delimiter: these payloads embed `)"`, which would close a
          // plain R"(...)" literal in the middle of the string.
          R"json({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":"background:url(https://x/y)"}})json",
          R"json({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":"width:expression(alert(1))"}})json",
          R"({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":"color:red}html{display:none"}})",
          R"({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":"content:'x'"}})",
          R"({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":";;; "}})",
          R"({"selector":[{"type":"css-selector","arg":"a"}],"action":{"type":"style","arg":""}})",
          // Two operators is not the plain shape; it stays procedural, and the
          // procedural path does not accept a style action.
          R"({"selector":[{"type":"css-selector","arg":"a"},{"type":"has-text","arg":"ad"}],"action":{"type":"style","arg":"color:red"}})",
      },
      {});

  EXPECT_TRUE(result.default_styled.empty());
  EXPECT_TRUE(result.default_actions.empty());
}

TEST(ProceduralCosmeticSanitizerTest,
     DeduplicatesAndSharesOneBoundedBudgetAcrossRuleGroups) {
  std::vector<std::string> default_actions;
  default_actions.reserve(64);
  for (size_t index = 0; index < 64; ++index) {
    default_actions.push_back(
        R"({"selector":[{"type":"css-selector","arg":".ad-)" +
        std::to_string(index) + R"("}]})");
  }
  const std::vector<std::string> additional_actions = {
      default_actions.front(),
      R"({"selector":[{"type":"css-selector","arg":".must-not-pass"}]})",
  };

  const SanitizedProceduralActionSets result =
      SanitizeProceduralActionSets(default_actions, additional_actions);

  EXPECT_EQ(64u, result.default_actions.size());
  EXPECT_TRUE(result.additional_actions.empty());
}

}  // namespace
}  // namespace seoul::adblock
