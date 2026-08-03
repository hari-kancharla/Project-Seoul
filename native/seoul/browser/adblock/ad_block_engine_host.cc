// Project Seoul asynchronous owner for the single-threaded Rust blocker.

#include "seoul/browser/adblock/ad_block_engine_host.h"

#include <algorithm>
#include <utility>

#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"

namespace seoul::adblock {
namespace {

void RemoveStandardModeProceduralSelectors(
    std::vector<std::string>* selectors) {
  std::erase_if(*selectors, [](const std::string& selector) {
    return selector.find(":has(") != std::string::npos;
  });
}

bool HasOwnDecision(const AdBlockMatchResult& result) {
  return result.important || result.matched_rule.has_value() ||
         result.exception_rule.has_value() || result.redirect.has_value() ||
         result.rewritten_url.has_value();
}

AdBlockMatchResult MergeResults(AdBlockMatchResult default_result,
                                AdBlockMatchResult additional_result) {
  additional_result.matched |= default_result.matched;
  additional_result.important |= default_result.important;
  additional_result.has_exception |= default_result.has_exception;
  if (!additional_result.matched_rule) {
    additional_result.matched_rule = std::move(default_result.matched_rule);
  }
  if (!additional_result.exception_rule) {
    additional_result.exception_rule = std::move(default_result.exception_rule);
  }
  if (!additional_result.redirect) {
    additional_result.redirect = std::move(default_result.redirect);
  }
  if (!additional_result.rewritten_url) {
    additional_result.rewritten_url = std::move(default_result.rewritten_url);
  }
  return additional_result;
}

}  // namespace

AdBlockEngineReplaceResult::AdBlockEngineReplaceResult() = default;
AdBlockEngineReplaceResult::AdBlockEngineReplaceResult(
    const AdBlockEngineReplaceResult&) = default;
AdBlockEngineReplaceResult& AdBlockEngineReplaceResult::operator=(
    const AdBlockEngineReplaceResult&) = default;

AdBlockEngineReplaceResult::AdBlockEngineReplaceResult(bool success,
                                                       std::string error)
    : success(success), error(std::move(error)) {}

AdBlockEngineReplaceResult::AdBlockEngineReplaceResult(
    AdBlockEngineReplaceResult&&) = default;
AdBlockEngineReplaceResult& AdBlockEngineReplaceResult::operator=(
    AdBlockEngineReplaceResult&&) = default;
AdBlockEngineReplaceResult::~AdBlockEngineReplaceResult() = default;

AdBlockCosmeticSelectorSet::AdBlockCosmeticSelectorSet() = default;
AdBlockCosmeticSelectorSet::AdBlockCosmeticSelectorSet(
    const AdBlockCosmeticSelectorSet&) = default;
AdBlockCosmeticSelectorSet& AdBlockCosmeticSelectorSet::operator=(
    const AdBlockCosmeticSelectorSet&) = default;
AdBlockCosmeticSelectorSet::AdBlockCosmeticSelectorSet(
    AdBlockCosmeticSelectorSet&&) = default;
AdBlockCosmeticSelectorSet& AdBlockCosmeticSelectorSet::operator=(
    AdBlockCosmeticSelectorSet&&) = default;
AdBlockCosmeticSelectorSet::~AdBlockCosmeticSelectorSet() = default;

AdBlockCosmeticResources::AdBlockCosmeticResources() = default;
AdBlockCosmeticResources::AdBlockCosmeticResources(
    const AdBlockCosmeticResources&) = default;
AdBlockCosmeticResources& AdBlockCosmeticResources::operator=(
    const AdBlockCosmeticResources&) = default;
AdBlockCosmeticResources::AdBlockCosmeticResources(AdBlockCosmeticResources&&) =
    default;
AdBlockCosmeticResources& AdBlockCosmeticResources::operator=(
    AdBlockCosmeticResources&&) = default;
AdBlockCosmeticResources::~AdBlockCosmeticResources() = default;

AdBlockDynamicCosmeticSelectors::AdBlockDynamicCosmeticSelectors() = default;
AdBlockDynamicCosmeticSelectors::AdBlockDynamicCosmeticSelectors(
    const AdBlockDynamicCosmeticSelectors&) = default;
AdBlockDynamicCosmeticSelectors& AdBlockDynamicCosmeticSelectors::operator=(
    const AdBlockDynamicCosmeticSelectors&) = default;
AdBlockDynamicCosmeticSelectors::AdBlockDynamicCosmeticSelectors(
    AdBlockDynamicCosmeticSelectors&&) = default;
AdBlockDynamicCosmeticSelectors& AdBlockDynamicCosmeticSelectors::operator=(
    AdBlockDynamicCosmeticSelectors&&) = default;
AdBlockDynamicCosmeticSelectors::~AdBlockDynamicCosmeticSelectors() = default;

AdBlockEngineWorker::AdBlockEngineWorker() {
  std::string error;
  default_engine_ = AdBlockEngine::Create({}, &error);
  additional_engine_ = AdBlockEngine::Create({}, &error);
}

AdBlockEngineWorker::~AdBlockEngineWorker() = default;

AdBlockEngineEvaluationResult AdBlockEngineWorker::Evaluate(
    AdBlockRequest request) {
  if (request.mode == AdBlockMode::kOff || !default_engine_ ||
      !additional_engine_) {
    return AdBlockEngineEvaluationResult();
  }

  AdBlockEngineEvaluationResult result = EvaluateOnce(request, true);
  if (!result.match.rewritten_url) {
    return result;
  }

  const GURL rewritten_url(*result.match.rewritten_url);
  if (!IsSafeAdBlockUrlRewrite(GURL(request.url), rewritten_url,
                               request.method) ||
      !RewriteAdBlockRequestUrl(&request, rewritten_url)) {
    result.match.rewritten_url.reset();
    if (!HasOwnDecision(result.match)) {
      result.deciding_engine = AdBlockDecidingEngine::kNone;
    }
    return result;
  }

  // A remove-parameter rule must never bypass a blocking rule on the target
  // URL. Re-evaluate the browser-normalized target against the same immutable
  // engines, while suppressing another rewrite to prevent chaining or loops.
  AdBlockEngineEvaluationResult target_result = EvaluateOnce(request, false);
  const bool target_is_blocked =
      target_result.match.important ||
      (target_result.match.matched && !target_result.match.has_exception);
  if (target_is_blocked) {
    return target_result;
  }
  return result;
}

AdBlockEngineEvaluationResult AdBlockEngineWorker::EvaluateOnce(
    const AdBlockRequest& request,
    bool allow_url_rewrite) {
  AdBlockMatchResult default_result = default_engine_->Evaluate(request);
  // Match Brave's two-engine policy: browser-vetted default filters never
  // rewrite user-facing URLs. Remove-parameter rules are a user/additional
  // list capability.
  default_result.rewritten_url.reset();

  const bool apply_default_ordinary_match =
      request.mode == AdBlockMode::kAggressive || request.is_third_party;
  if (!apply_default_ordinary_match && !default_result.important) {
    default_result.matched = false;
    default_result.matched_rule.reset();
    default_result.redirect.reset();
  }

  if (default_result.important) {
    return AdBlockEngineEvaluationResult{std::move(default_result),
                                         AdBlockDecidingEngine::kDefault};
  }

  AdBlockMatchResult additional_result = additional_engine_->Evaluate(
      request, default_result.matched, default_result.has_exception);
  if (!allow_url_rewrite) {
    additional_result.rewritten_url.reset();
  }
  const AdBlockDecidingEngine deciding_engine =
      HasOwnDecision(additional_result)
          ? AdBlockDecidingEngine::kAdditional
          : (HasOwnDecision(default_result) ? AdBlockDecidingEngine::kDefault
                                            : AdBlockDecidingEngine::kNone);
  return AdBlockEngineEvaluationResult{
      MergeResults(std::move(default_result), std::move(additional_result)),
      deciding_engine};
}

AdBlockCosmeticResources AdBlockEngineWorker::GetCosmeticResources(
    std::string url,
    AdBlockMode mode) {
  AdBlockCosmeticResources result;
  if (mode == AdBlockMode::kOff || !default_engine_ || !additional_engine_) {
    return result;
  }

  AdBlockCosmeticEngineResources default_resources =
      default_engine_->GetUrlCosmeticResources(url);
  AdBlockCosmeticEngineResources additional_resources =
      additional_engine_->GetUrlCosmeticResources(url);
  if (mode == AdBlockMode::kStandard) {
    RemoveStandardModeProceduralSelectors(&default_resources.hide_selectors);
    default_resources.procedural_actions.clear();
  }

  result.enabled = true;
  result.default_rules.selectors = std::move(default_resources.hide_selectors);
  result.default_rules.isolated_script =
      std::move(default_resources.isolated_script);
  result.default_rules.procedural_actions =
      std::move(default_resources.procedural_actions);
  result.additional_rules.selectors =
      std::move(additional_resources.hide_selectors);
  result.additional_rules.isolated_script =
      std::move(additional_resources.isolated_script);
  result.additional_rules.procedural_actions =
      std::move(additional_resources.procedural_actions);
  const bool query_generics =
      !default_resources.generichide && !additional_resources.generichide;
  result.default_rules.query_generics = query_generics;
  result.additional_rules.query_generics = query_generics;
  return result;
}

AdBlockDynamicCosmeticSelectors
AdBlockEngineWorker::GetDynamicCosmeticSelectors(
    std::string url,
    AdBlockMode mode,
    std::vector<std::string> classes,
    std::vector<std::string> ids) {
  AdBlockDynamicCosmeticSelectors result;
  if (mode == AdBlockMode::kOff || !default_engine_ || !additional_engine_) {
    return result;
  }

  const AdBlockCosmeticEngineResources default_resources =
      default_engine_->GetUrlCosmeticResources(url);
  const AdBlockCosmeticEngineResources additional_resources =
      additional_engine_->GetUrlCosmeticResources(url);
  if (default_resources.generichide || additional_resources.generichide) {
    return result;
  }

  std::vector<std::string> exceptions = default_resources.exceptions;
  exceptions.insert(exceptions.end(), additional_resources.exceptions.begin(),
                    additional_resources.exceptions.end());
  std::ranges::sort(exceptions);
  exceptions.erase(std::ranges::unique(exceptions).begin(), exceptions.end());

  result.default_selectors =
      default_engine_->GetHiddenClassIdSelectors(classes, ids, exceptions);
  if (mode == AdBlockMode::kStandard) {
    RemoveStandardModeProceduralSelectors(&result.default_selectors);
  }
  result.additional_selectors =
      additional_engine_->GetHiddenClassIdSelectors(classes, ids, exceptions);
  return result;
}

AdBlockEngineReplaceResult AdBlockEngineWorker::ReplaceRules(
    AdBlockEngineGroup group,
    std::vector<uint8_t> rules) {
  std::string error;
  std::unique_ptr<AdBlockEngine> replacement =
      AdBlockEngine::Create(rules, &error);
  if (!replacement) {
    return AdBlockEngineReplaceResult(false, std::move(error));
  }
  if (group == AdBlockEngineGroup::kDefault) {
    default_engine_ = std::move(replacement);
  } else {
    additional_engine_ = std::move(replacement);
  }
  return AdBlockEngineReplaceResult(true, std::string());
}

AdBlockEngineReplaceResult AdBlockEngineWorker::ReplaceRuleSets(
    std::vector<uint8_t> default_rules,
    std::vector<uint8_t> additional_rules) {
  std::string default_error;
  std::unique_ptr<AdBlockEngine> default_replacement =
      AdBlockEngine::Create(default_rules, &default_error);
  if (!default_replacement) {
    return AdBlockEngineReplaceResult(
        false, "default rules: " + std::move(default_error));
  }

  std::string additional_error;
  std::unique_ptr<AdBlockEngine> additional_replacement =
      AdBlockEngine::Create(additional_rules, &additional_error);
  if (!additional_replacement) {
    return AdBlockEngineReplaceResult(
        false, "additional rules: " + std::move(additional_error));
  }

  // Both replacements are constructed before either live pointer moves. A
  // malformed update therefore cannot leave the two-engine policy half
  // updated.
  default_engine_ = std::move(default_replacement);
  additional_engine_ = std::move(additional_replacement);
  return AdBlockEngineReplaceResult(true, std::string());
}

AdBlockEngineHost::AdBlockEngineHost()
    : AdBlockEngineHost(base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

AdBlockEngineHost::AdBlockEngineHost(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : worker_(std::move(task_runner)) {}

AdBlockEngineHost::~AdBlockEngineHost() = default;

void AdBlockEngineHost::Evaluate(AdBlockRequest request,
                                 MatchCallback callback) {
  worker_.AsyncCall(&AdBlockEngineWorker::Evaluate)
      .WithArgs(std::move(request))
      .Then(std::move(callback));
}

void AdBlockEngineHost::GetCosmeticResources(
    std::string url,
    AdBlockMode mode,
    CosmeticResourcesCallback callback) {
  worker_.AsyncCall(&AdBlockEngineWorker::GetCosmeticResources)
      .WithArgs(std::move(url), mode)
      .Then(std::move(callback));
}

void AdBlockEngineHost::GetDynamicCosmeticSelectors(
    std::string url,
    AdBlockMode mode,
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    DynamicCosmeticSelectorsCallback callback) {
  worker_.AsyncCall(&AdBlockEngineWorker::GetDynamicCosmeticSelectors)
      .WithArgs(std::move(url), mode, std::move(classes), std::move(ids))
      .Then(std::move(callback));
}

void AdBlockEngineHost::ReplaceRules(std::vector<uint8_t> rules,
                                     ReplaceCallback callback) {
  ReplaceRules(AdBlockEngineGroup::kDefault, std::move(rules),
               std::move(callback));
}

void AdBlockEngineHost::ReplaceRules(AdBlockEngineGroup group,
                                     std::vector<uint8_t> rules,
                                     ReplaceCallback callback) {
  worker_.AsyncCall(&AdBlockEngineWorker::ReplaceRules)
      .WithArgs(group, std::move(rules))
      .Then(std::move(callback));
}

void AdBlockEngineHost::ReplaceRuleSets(std::vector<uint8_t> default_rules,
                                        std::vector<uint8_t> additional_rules,
                                        ReplaceCallback callback) {
  worker_.AsyncCall(&AdBlockEngineWorker::ReplaceRuleSets)
      .WithArgs(std::move(default_rules), std::move(additional_rules))
      .Then(std::move(callback));
}

}  // namespace seoul::adblock
