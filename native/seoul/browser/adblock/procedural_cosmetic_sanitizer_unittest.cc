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
              R"({"selector":[{"type":"css-selector","arg":".ad"}],"action":{"type":"style","arg":"opacity:0"}})",
              R"({"selector":[{"type":"css-selector","arg":"body{display:none}"}]})",
              R"({"selector":[{"type":"css-selector","arg":".ad"}],"extra":true})",
              R"({"selector":[{"type":"css-selector","arg":".ad"}],"action":{"type":"remove-attr","arg":"onclick;alert(1)"}})",
              R"({"selector":[]})",
              R"({"selector":"not-a-list"})",
              "not-json",
          },
          {});

  EXPECT_TRUE(result.default_actions.empty());
  EXPECT_TRUE(result.additional_actions.empty());
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
