// Copyright 2026 The Project Seoul Authors
// Browser-authoritative validation for data-only procedural cosmetic rules.

#ifndef SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_
#define SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_

#include <string>
#include <vector>

namespace seoul::adblock {

// A `:style()` rule reduced to plain CSS.
//
// The overwhelming majority of `:style()` rules are one selector and one
// declaration block with no procedural operator at all - they are a stylesheet
// rule wearing procedural clothing. Emitting them as CSS costs the renderer
// nothing per element, where evaluating them procedurally would walk the DOM
// for no reason. These are the rules that unlock scrolling after a consent
// modal is hidden, so dropping them is what makes hiding a consent notice leave
// the page unusable.
struct StyledSelector {
  std::string selector;
  std::string declarations;
};

struct SanitizedProceduralActionSets {
  SanitizedProceduralActionSets();
  SanitizedProceduralActionSets(const SanitizedProceduralActionSets&);
  SanitizedProceduralActionSets& operator=(const SanitizedProceduralActionSets&);
  SanitizedProceduralActionSets(SanitizedProceduralActionSets&&);
  SanitizedProceduralActionSets& operator=(SanitizedProceduralActionSets&&);
  ~SanitizedProceduralActionSets();

  std::vector<std::string> default_actions;
  std::vector<std::string> additional_actions;
  std::vector<StyledSelector> default_styled;
  std::vector<StyledSelector> additional_styled;
};

// Parses list-provided JSON as data, retains only Seoul's fixed supported
// operator/action schema, canonicalizes it, deduplicates it, and applies one
// shared bounded budget across both rule groups.
SanitizedProceduralActionSets SanitizeProceduralActionSets(
    const std::vector<std::string>& default_actions,
    const std::vector<std::string>& additional_actions);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_
