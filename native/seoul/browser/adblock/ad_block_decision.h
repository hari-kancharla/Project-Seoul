// Project Seoul native blocker structured decision.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DECISION_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DECISION_H_

#include <optional>
#include <string>

namespace seoul::adblock {

enum class AdBlockAction {
  kAllow,
  kBlock,
  kRedirect,
  kRewrite,
};

enum class AdBlockDecidingEngine {
  kNone,
  kDefault,
  kAdditional,
};

enum class AdBlockRuleCategory {
  kNone,
  kNetwork,
  kException,
  kImportant,
  kRedirect,
  kRewrite,
};

struct AdBlockDecision {
  AdBlockDecision();
  AdBlockDecision(const AdBlockDecision&);
  AdBlockDecision& operator=(const AdBlockDecision&);
  AdBlockDecision(AdBlockDecision&&);
  AdBlockDecision& operator=(AdBlockDecision&&);
  ~AdBlockDecision();

  AdBlockAction action = AdBlockAction::kAllow;
  AdBlockDecidingEngine deciding_engine = AdBlockDecidingEngine::kNone;
  AdBlockRuleCategory rule_category = AdBlockRuleCategory::kNone;
  bool matched = false;
  bool important = false;
  bool has_exception = false;
  std::optional<std::string> matched_rule;
  std::optional<std::string> exception_rule;
  std::optional<std::string> redirect;
  std::optional<std::string> rewritten_url;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DECISION_H_
