// Project Seoul asynchronous owner for the single-threaded Rust blocker.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_HOST_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_HOST_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "seoul/browser/adblock/ad_block_decision.h"
#include "seoul/browser/adblock/ad_block_engine.h"

namespace seoul::adblock {

enum class AdBlockEngineGroup {
  kDefault,
  kAdditional,
};

struct AdBlockEngineReplaceResult {
  AdBlockEngineReplaceResult();
  AdBlockEngineReplaceResult(const AdBlockEngineReplaceResult&);
  AdBlockEngineReplaceResult& operator=(const AdBlockEngineReplaceResult&);
  AdBlockEngineReplaceResult(bool success, std::string error);
  AdBlockEngineReplaceResult(AdBlockEngineReplaceResult&&);
  AdBlockEngineReplaceResult& operator=(AdBlockEngineReplaceResult&&);
  ~AdBlockEngineReplaceResult();

  bool success = false;
  std::string error;
};

struct AdBlockEngineEvaluationResult {
  AdBlockMatchResult match;
  AdBlockDecidingEngine deciding_engine = AdBlockDecidingEngine::kNone;
};

struct AdBlockCosmeticSelectorSet {
  AdBlockCosmeticSelectorSet();
  AdBlockCosmeticSelectorSet(const AdBlockCosmeticSelectorSet&);
  AdBlockCosmeticSelectorSet& operator=(const AdBlockCosmeticSelectorSet&);
  AdBlockCosmeticSelectorSet(AdBlockCosmeticSelectorSet&&);
  AdBlockCosmeticSelectorSet& operator=(AdBlockCosmeticSelectorSet&&);
  ~AdBlockCosmeticSelectorSet();

  std::vector<std::string> selectors;
  std::string isolated_script;
  std::vector<std::string> procedural_actions;
  bool query_generics = false;
};

struct AdBlockCosmeticResources {
  AdBlockCosmeticResources();
  AdBlockCosmeticResources(const AdBlockCosmeticResources&);
  AdBlockCosmeticResources& operator=(const AdBlockCosmeticResources&);
  AdBlockCosmeticResources(AdBlockCosmeticResources&&);
  AdBlockCosmeticResources& operator=(AdBlockCosmeticResources&&);
  ~AdBlockCosmeticResources();

  bool enabled = false;
  AdBlockCosmeticSelectorSet default_rules;
  AdBlockCosmeticSelectorSet additional_rules;
};

struct AdBlockDynamicCosmeticSelectors {
  AdBlockDynamicCosmeticSelectors();
  AdBlockDynamicCosmeticSelectors(const AdBlockDynamicCosmeticSelectors&);
  AdBlockDynamicCosmeticSelectors& operator=(
      const AdBlockDynamicCosmeticSelectors&);
  AdBlockDynamicCosmeticSelectors(AdBlockDynamicCosmeticSelectors&&);
  AdBlockDynamicCosmeticSelectors& operator=(AdBlockDynamicCosmeticSelectors&&);
  ~AdBlockDynamicCosmeticSelectors();

  std::vector<std::string> default_selectors;
  std::vector<std::string> additional_selectors;
};

class AdBlockEngineWorker {
 public:
  AdBlockEngineWorker();
  ~AdBlockEngineWorker();

  AdBlockEngineWorker(const AdBlockEngineWorker&) = delete;
  AdBlockEngineWorker& operator=(const AdBlockEngineWorker&) = delete;

  AdBlockEngineEvaluationResult Evaluate(AdBlockRequest request);
  // Combined `$csp` directives from both engines for a document/subdocument
  // navigation. Empty when the mode is Off or nothing matches.
  std::string GetCspDirectives(AdBlockRequest request, AdBlockMode mode);
  AdBlockCosmeticResources GetCosmeticResources(std::string url,
                                                AdBlockMode mode);
  AdBlockDynamicCosmeticSelectors GetDynamicCosmeticSelectors(
      std::string url,
      AdBlockMode mode,
      std::vector<std::string> classes,
      std::vector<std::string> ids);
  AdBlockEngineReplaceResult ReplaceRules(AdBlockEngineGroup group,
                                          std::vector<uint8_t> rules);
  AdBlockEngineReplaceResult ReplaceRuleSets(
      std::vector<uint8_t> default_rules,
      std::vector<uint8_t> additional_rules);

 private:
  AdBlockEngineEvaluationResult EvaluateOnce(const AdBlockRequest& request,
                                             bool allow_url_rewrite);

  std::unique_ptr<AdBlockEngine> default_engine_;
  std::unique_ptr<AdBlockEngine> additional_engine_;
};

class AdBlockEngineHost {
 public:
  using MatchCallback = base::OnceCallback<void(AdBlockEngineEvaluationResult)>;
  using ReplaceCallback = base::OnceCallback<void(AdBlockEngineReplaceResult)>;
  using CspDirectivesCallback = base::OnceCallback<void(std::string)>;
  using CosmeticResourcesCallback =
      base::OnceCallback<void(AdBlockCosmeticResources)>;
  using DynamicCosmeticSelectorsCallback =
      base::OnceCallback<void(AdBlockDynamicCosmeticSelectors)>;

  AdBlockEngineHost();
  explicit AdBlockEngineHost(
      scoped_refptr<base::SequencedTaskRunner> task_runner);
  ~AdBlockEngineHost();

  AdBlockEngineHost(const AdBlockEngineHost&) = delete;
  AdBlockEngineHost& operator=(const AdBlockEngineHost&) = delete;

  void Evaluate(AdBlockRequest request, MatchCallback callback);
  void GetCspDirectives(AdBlockRequest request,
                        AdBlockMode mode,
                        CspDirectivesCallback callback);
  void GetCosmeticResources(std::string url,
                            AdBlockMode mode,
                            CosmeticResourcesCallback callback);
  void GetDynamicCosmeticSelectors(std::string url,
                                   AdBlockMode mode,
                                   std::vector<std::string> classes,
                                   std::vector<std::string> ids,
                                   DynamicCosmeticSelectorsCallback callback);
  void ReplaceRules(std::vector<uint8_t> rules, ReplaceCallback callback);
  void ReplaceRules(AdBlockEngineGroup group,
                    std::vector<uint8_t> rules,
                    ReplaceCallback callback);
  void ReplaceRuleSets(std::vector<uint8_t> default_rules,
                       std::vector<uint8_t> additional_rules,
                       ReplaceCallback callback);

 private:
  base::SequenceBound<AdBlockEngineWorker> worker_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_ENGINE_HOST_H_
