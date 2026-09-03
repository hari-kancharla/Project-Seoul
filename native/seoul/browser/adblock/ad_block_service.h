// Project Seoul profile-aware native blocker coordinator.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_H_

#include <map>
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
#include "seoul/browser/adblock/ad_block_catalogue_subscriber.h"
#include "seoul/browser/adblock/ad_block_filter_list_manager.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "seoul/browser/adblock/ad_block_stats_service.h"
#include "seoul/browser/adblock/ad_block_subscription_downloader.h"

class Profile;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace seoul::adblock {

class AdBlockRequestInterceptor;

struct AdBlockBlockedNavigation {
  std::string url;
  AdBlockDecision decision;
};

// What one site is told about this machine for the rest of the session.
struct SiteIdentity {
  // False when nothing is farbled for the site (protection Off, or shields
  // Off): the reported values are then the real ones.
  bool farbled = false;
  uint64_t token = 0;
  // Four hex digits a person can compare across sites and across a rotation.
  std::string persona;
  int real_cores = 0;
  unsigned reported_cores = 0;
  // NOT this machine's RAM. The Device Memory specification requires a clamped
  // power-of-two bucket, so Chromium already reports the same coarse class to
  // every site on every browser. Calling it "real" in the interface told people
  // their memory had been disclosed when it never was.
  float memory_class_gib = 0.0f;
  float reported_memory_gib = 0.0f;
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
  // `$csp` directives for a document/subdocument navigation. `document_url` is
  // the site whose mode governs injection; the result is empty when that site
  // is Off, so a disabled site never receives an injected policy.
  void GetCspDirectives(AdBlockRequest request,
                        const GURL& document_url,
                        AdBlockEngineHost::CspDirectivesCallback callback);
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

  // A settings view bound to `context` rather than to the profile that owns
  // this service.
  //
  // One service serves a profile and its off-the-record sessions, so that a
  // private window gets real blocking and real fingerprinting protection
  // instead of none. Its SETTINGS must not be shared the same way: an
  // off-the-record context has its own content-settings map, which inherits
  // the regular profile's values and discards its own writes when the session
  // ends. Reading through it means a private window honours the choices a
  // person already made, and writing through it means a change made there
  // cannot outlive the window - which is the whole point of a private window.
  //
  // Returns null only when `context` has no profile.
  std::unique_ptr<AdBlockSettings> SettingsFor(
      content::BrowserContext* context) const;
  void SetDefaultMode(AdBlockMode mode);
  void SetSiteMode(const GURL& site_url, std::optional<AdBlockMode> mode);
  FingerprintMode GetDefaultFingerprintMode() const;
  void SetDefaultFingerprintMode(FingerprintMode mode);
  // A site's own fingerprint mode; nullopt returns it to the profile default.
  void SetSiteFingerprintMode(const GURL& site_url,
                              std::optional<FingerprintMode> mode);
  // Strict-only entry point kept for callers that predate the modes.
  void SetCanvasFingerprintBlocked(const GURL& site_url, bool blocked);
  // Deterministic per-site farbling token for this browsing session: same
  // site in the same browser context, same token; a different site, a
  // different context (incognito is served by this service but must never
  // share the regular profile's pattern), or a restarted browser, a different
  // token. Never 0, so a set token always farbles.
  //
  // 64 bits deliberately. A script can draw pixels it chose, read them back,
  // and watch how they were perturbed - an unlimited known-plaintext oracle
  // against this value. A 32-bit secret falls to an offline search in seconds
  // under that, and every farbled answer becomes predictable.
  uint64_t GetFarblingToken(const GURL& site_url,
                            const std::string& scope) const;

  // The scope an identity is remembered in.
  //
  // Storage is what a site can use to recognise a person, and Seoul splits it
  // two ways: by profile (incognito from regular) and, for an isolated Space,
  // by that Space's own StoragePartition inside one profile. An identity has to
  // be split exactly as far, or the fingerprint re-links what the storage split
  // separated - the same site in two Spaces would read the same canvas noise
  // and the same core count, and the panel would print one persona for both.
  static std::string IdentityScopeFor(content::WebContents* contents);
  static std::string IdentityScopeFor(const content::BrowserContext* context);
  // Gives the site a fresh pattern for the rest of the session - a new
  // canvas noise, a new hardware profile - without touching any other site.
  void RotateIdentity(const GURL& site_url, const std::string& scope);
  // What the site is told about this machine, computed from the same token
  // and the same shared generator the renderer uses, so the panel can show
  // a person exactly what a site saw.
  SiteIdentity DescribeIdentity(const GURL& site_url,
                                const std::string& scope) const;
  void TemporarilyDisable(const GURL& site_url, base::TimeDelta duration);
  void ClearTemporaryDisable(const GURL& site_url);

  AdBlockStatsService* stats() { return &stats_; }
  AdBlockFilterListUpdateStatus filter_list_status() const;

  // Fetches one catalogued list through the shared downloader.
  void FetchCatalogueEntry(const AdBlockCatalogEntry& entry,
                           AdBlockCatalogueSubscriber::FetchCallback done);
  // Installs a completed round into the default engine.
  void InstallCatalogueLists(std::string rules, base::OnceClosure done);
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
  // Two downloaders on purpose. Each one owns a single SimpleURLLoader and
  // refuses a second concurrent request, so sharing one between the pinned
  // path and the catalogue subscriber meant whichever started first made the
  // other fail with "a filter-list download is already in progress" - and the
  // subscriber starts one the moment the service is constructed, so in
  // practice it was always the pinned path that lost.
  std::unique_ptr<AdBlockSubscriptionDownloader> subscription_downloader_;
  std::unique_ptr<AdBlockSubscriptionDownloader> catalogue_downloader_;
  std::unique_ptr<AdBlockCatalogueSubscriber> catalogue_subscriber_;
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
