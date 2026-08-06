// Project Seoul native blocker engine.
// Owns a pinned adblock-rust engine on one sequence. Chromium integration
// supplies already-parsed request metadata; this layer performs no I/O.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/sequence_checker.h"
#include "seoul/browser/adblock/ad_block_request.h"

namespace seoul::adblock {

struct AdBlockMatchResult {
  AdBlockMatchResult();
  AdBlockMatchResult(const AdBlockMatchResult&);
  AdBlockMatchResult& operator=(const AdBlockMatchResult&);
  AdBlockMatchResult(AdBlockMatchResult&&);
  AdBlockMatchResult& operator=(AdBlockMatchResult&&);
  AdBlockMatchResult(bool matched,
                     bool important,
                     bool has_exception,
                     std::optional<std::string> matched_rule,
                     std::optional<std::string> exception_rule,
                     std::optional<std::string> redirect,
                     std::optional<std::string> rewritten_url);
  ~AdBlockMatchResult();

  bool matched = false;
  bool important = false;
  bool has_exception = false;
  std::optional<std::string> matched_rule;
  std::optional<std::string> exception_rule;
  std::optional<std::string> redirect;
  std::optional<std::string> rewritten_url;
};

struct AdBlockCosmeticEngineResources {
  AdBlockCosmeticEngineResources();
  AdBlockCosmeticEngineResources(const AdBlockCosmeticEngineResources&);
  AdBlockCosmeticEngineResources& operator=(
      const AdBlockCosmeticEngineResources&);
  AdBlockCosmeticEngineResources(AdBlockCosmeticEngineResources&&);
  AdBlockCosmeticEngineResources& operator=(AdBlockCosmeticEngineResources&&);
  ~AdBlockCosmeticEngineResources();

  std::vector<std::string> hide_selectors;
  std::vector<std::string> exceptions;
  std::string isolated_script;
  std::vector<std::string> procedural_actions;
  bool generichide = false;
};

class AdBlockEngine {
 public:
  static std::unique_ptr<AdBlockEngine> Create(base::span<const uint8_t> rules,
                                               std::string* error);

  AdBlockEngine(const AdBlockEngine&) = delete;
  AdBlockEngine& operator=(const AdBlockEngine&) = delete;
  ~AdBlockEngine();

  AdBlockMatchResult Evaluate(const AdBlockRequest& request,
                              bool previously_matched_rule = false,
                              bool previously_matched_exception = false) const;
  // Combined `$csp` directives for a document/subdocument request, or an empty
  // string when no policy applies. The engine resolves `$csp` exceptions and
  // merges multiple matching directives; callers only ever append the result as
  // an additional policy, which by CSP semantics can restrict but never relax.
  std::string GetCspDirectives(const AdBlockRequest& request) const;
  AdBlockCosmeticEngineResources GetUrlCosmeticResources(
      const std::string& url) const;
  std::vector<std::string> GetHiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions) const;
  std::vector<uint8_t> Serialize() const;
  bool Deserialize(base::span<const uint8_t> serialized);

 private:
  class Impl;

  explicit AdBlockEngine(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_H_
