// Project Seoul profile-aware native blocker coordinator.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "seoul/browser/adblock/ad_block_decision.h"
#include "seoul/browser/adblock/ad_block_engine_host.h"
#include "seoul/browser/adblock/ad_block_filter_list_manager.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "seoul/browser/adblock/ad_block_stats_service.h"
#include "seoul/browser/adblock/ad_block_subscription_downloader.h"

class Profile;

namespace seoul::adblock {

class AdBlockRequestInterceptor;

struct AdBlockBlockedNavigation {
  std::string url;
  AdBlockDecision decision;
};

class AdBlockService : public KeyedService {
 public:
  using DecisionCallback = base::OnceCallback<void(AdBlockDecision)>;
  using CosmeticResourcesCallback =
      AdBlockEngineHost::CosmeticResourcesCallback;
  using DynamicCosmeticSelectorsCallback =
      AdBlockEngineHost::DynamicCosmeticSelectorsCallback;

  explicit AdBlockService(Profile* profile);
  ~AdBlockService() override;

  AdBlockService(const AdBlockService&) = delete;
  AdBlockService& operator=(const AdBlockService&) = delete;

  void CheckRequest(AdBlockRequest request, DecisionCallback callback);
  void GetCosmeticResources(const GURL& document_url,
                            CosmeticResourcesCallback callback);
  void GetDynamicCosmeticSelectors(const GURL& document_url,
                                   std::vector<std::string> classes,
                                   std::vector<std::string> ids,
                                   DynamicCosmeticSelectorsCallback callback);
  void ReplaceRulesForTesting(std::vector<uint8_t> rules,
                              AdBlockEngineHost::ReplaceCallback callback);
  void ReplaceAdditionalRulesForTesting(
      std::vector<uint8_t> rules,
      AdBlockEngineHost::ReplaceCallback callback);
  void ActivateVerifiedFilterComponent(
      const base::FilePath& component_path,
      const base::Version& component_version,
      AdBlockFilterListManager::CompletionCallback callback);
  void DownloadPinnedAdditionalRuleSet(
      const GURL& subscription_url,
      std::string expected_sha256,
      const base::Version& rule_set_version,
      AdBlockFilterListManager::CompletionCallback callback);

  AdBlockSiteSettings GetSiteSettings(const GURL& site_url) const;
  void SetDefaultMode(AdBlockMode mode);
  void SetSiteMode(const GURL& site_url, std::optional<AdBlockMode> mode);
  void TemporarilyDisable(const GURL& site_url, base::TimeDelta duration);
  void ClearTemporaryDisable(const GURL& site_url);

  AdBlockStatsService* stats() { return &stats_; }
  AdBlockFilterListUpdateStatus filter_list_status() const;
  const std::optional<AdBlockBlockedNavigation>& last_blocked_navigation()
      const {
    return last_blocked_navigation_;
  }
  base::WeakPtr<AdBlockService> GetWeakPtr();

  // KeyedService:
  void Shutdown() override;

 private:
  friend class AdBlockRequestInterceptor;

  void AddRequestInterceptor(
      std::unique_ptr<AdBlockRequestInterceptor> interceptor);
  void RemoveRequestInterceptor(AdBlockRequestInterceptor* interceptor);
  void DisableFilterListManagerForTesting();
  void OnPinnedAdditionalRuleSetDownloaded(
      base::Version rule_set_version,
      AdBlockFilterListManager::CompletionCallback callback,
      AdBlockSubscriptionDownloadResult result);
  void OnEvaluated(
      std::optional<content::GlobalRenderFrameHostToken> frame_token,
      std::optional<std::string> navigation_url,
      std::string original_url,
      std::string method,
      AdBlockFactoryType factory_type,
      DecisionCallback callback,
      AdBlockEngineEvaluationResult result);

  const raw_ptr<Profile> profile_;
  bool shutdown_ = false;
  AdBlockEngineHost engine_host_;
  std::unique_ptr<AdBlockFilterListManager> filter_list_manager_;
  std::unique_ptr<AdBlockSubscriptionDownloader> subscription_downloader_;
  AdBlockSettings settings_;
  AdBlockStatsService stats_;
  std::optional<AdBlockBlockedNavigation> last_blocked_navigation_;
  std::set<std::unique_ptr<AdBlockRequestInterceptor>,
           base::UniquePtrComparator>
      request_interceptors_;
  base::WeakPtrFactory<AdBlockService> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_H_
