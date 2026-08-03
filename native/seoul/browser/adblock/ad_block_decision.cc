// Project Seoul native blocker structured decision.

#include "seoul/browser/adblock/ad_block_decision.h"

namespace seoul::adblock {

AdBlockDecision::AdBlockDecision() = default;
AdBlockDecision::AdBlockDecision(const AdBlockDecision&) = default;
AdBlockDecision& AdBlockDecision::operator=(const AdBlockDecision&) = default;
AdBlockDecision::AdBlockDecision(AdBlockDecision&&) = default;
AdBlockDecision& AdBlockDecision::operator=(AdBlockDecision&&) = default;
AdBlockDecision::~AdBlockDecision() = default;

}  // namespace seoul::adblock
