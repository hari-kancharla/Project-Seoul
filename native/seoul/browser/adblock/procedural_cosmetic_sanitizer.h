// Copyright 2026 The Project Seoul Authors
// Browser-authoritative validation for data-only procedural cosmetic rules.

#ifndef SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_
#define SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_

#include <string>
#include <vector>

namespace seoul::adblock {

struct SanitizedProceduralActionSets {
  std::vector<std::string> default_actions;
  std::vector<std::string> additional_actions;
};

// Parses list-provided JSON as data, retains only Seoul's fixed supported
// operator/action schema, canonicalizes it, deduplicates it, and applies one
// shared bounded budget across both rule groups.
SanitizedProceduralActionSets SanitizeProceduralActionSets(
    const std::vector<std::string>& default_actions,
    const std::vector<std::string>& additional_actions);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_PROCEDURAL_COSMETIC_SANITIZER_H_
