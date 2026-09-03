// Project Seoul profile-aware native blocker coordinator.

#include <algorithm>

#include "base/hash/hash.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/strings/string_split.h"
#include "chrome/browser/browser_process.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/common/device_memory/approximated_device_memory.h"
#include "third_party/blink/public/common/privacy/seoul_farbling.h"
#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_service.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_url_detracker.h"

#include "base/logging.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "seoul/browser/adblock/ad_block_request_interceptor.h"
#include "seoul/browser/adblock/ad_block_resource_catalog.h"

namespace seoul::adblock {
namespace {

GURL SiteUrlForRequest(const AdBlockRequest& request) {
  const GURL top_frame_url(request.outermost_top_frame_url);
  if (top_frame_url.is_valid() && top_frame_url.SchemeIsHTTPOrHTTPS()) {
    return top_frame_url;
  }
  const GURL initiator_url(request.initiator_url);
  if (initiator_url.is_valid() && initiator_url.SchemeIsHTTPOrHTTPS()) {
    return initiator_url;
  }
  return GURL(request.url);
}

}  // namespace

AdBlockService::AdBlockService(Profile* profile)
    : profile_(profile),
      settings_(profile ? profile->GetPrefs() : nullptr,
                profile ? HostContentSettingsMapFactory::GetForProfile(profile)
                        : nullptr),
      farbling_session_key_(base::RandUint64()) {
  if (profile_) {
    filter_list_manager_ = std::make_unique<AdBlockFilterListManager>(
        &engine_host_, profile_->GetPath(), profile_->GetPrefs());
    subscription_downloader_ = std::make_unique<AdBlockSubscriptionDownloader>(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess());
    catalogue_downloader_ = std::make_unique<AdBlockSubscriptionDownloader>(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess());
    filter_list_manager_->Start(base::DoNothing());

    // The catalog declares EasyList and EasyPrivacy as enabled-by-default
    // runtime downloads. Until this was wired, nothing acted on that: the
    // browser ran on the bundled baseline and the ad-serving class loaded
    // normally. Started after the manager so a first run serves the cache or
    // the baseline immediately and upgrades when the fetch lands, rather than
    // blocking startup on the network.
    // Regional lists follow the person, not a hidden toggle: the profile's
    // accept-languages plus the application locale decide which language
    // communities' lists ride along with the global ones.
    std::vector<std::string> languages = base::SplitString(
        profile_->GetPrefs()->GetString(language::prefs::kAcceptLanguages),
        ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    languages.push_back(g_browser_process->GetApplicationLocale());
    catalogue_subscriber_ = std::make_unique<AdBlockCatalogueSubscriber>(
        base::BindRepeating(&AdBlockService::FetchCatalogueEntry,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(&AdBlockService::InstallCatalogueLists,
                            weak_factory_.GetWeakPtr()),
        std::move(languages));
    catalogue_subscriber_->Start();
  }
}

void AdBlockService::FetchCatalogueEntry(
    const AdBlockCatalogEntry& entry,
    AdBlockCatalogueSubscriber::FetchCallback done) {
  if (shutdown_ || !catalogue_downloader_) {
    std::move(done).Run(false, std::string(),
                        "filter-list downloader is unavailable");
    return;
  }
  catalogue_downloader_->Download(
      GURL(entry.url), AdBlockSubscriptionIntegrity::kCataloguedHttps,
      std::string(),
      base::BindOnce(
          [](AdBlockCatalogueSubscriber::FetchCallback done,
             AdBlockSubscriptionDownloadResult result) {
            std::move(done).Run(result.success, std::move(result.rules),
                                std::move(result.error));
          },
          std::move(done)));
}

void AdBlockService::InstallCatalogueLists(std::string rules,
                                           base::OnceClosure done) {
  if (shutdown_ || !filter_list_manager_) {
    std::move(done).Run();
    return;
  }
  filter_list_manager_->ActivateCatalogueDefaultLists(
      std::move(rules), base::Version("1.0.0"),
      base::BindOnce(
          [](base::OnceClosure done, AdBlockFilterListUpdateStatus status) {
            std::move(done).Run();
          },
          std::move(done)));
}
AdBlockService::~AdBlockService() = default;

void AdBlockService::CheckRequest(AdBlockRequest request,
                                  DecisionCallback callback) {
  if (!IsSupportedRequestScheme(GURL(request.url))) {
    std::move(callback).Run(AdBlockDecision());
    return;
  }
  if (profile_) {
    request.mode =
        settings_.GetSiteSettings(SiteUrlForRequest(request)).effective_mode;
  }
  if (shutdown_ || request.mode == AdBlockMode::kOff) {
    std::move(callback).Run(AdBlockDecision());
    return;
  }

  std::optional<content::GlobalRenderFrameHostToken> frame_token =
      request.render_frame_token;
  std::optional<std::string> navigation_url;
  if (request.request_type == "main_frame") {
    navigation_url = request.url;
  }
  std::string original_url = request.url;
  std::string method = request.method;
  const AdBlockFactoryType factory_type = request.factory_type;
  // Which requests may have tracking parameters stripped.
  //
  // Navigations in both parties, because that is the case a person actually
  // sees: the identifier sits in the address bar after a click. Subresources
  // only when third-party, so a site's own first-party call keeps its own
  // parameters and nothing a page depends on is disturbed. Never WebSockets:
  // the WebSocket interceptor treats any non-allow decision as a block, so a
  // rewrite there would silently sever the connection.
  const bool detrack_eligible =
      request.factory_type != AdBlockFactoryType::kWebSocket &&
      GURL(request.url).SchemeIsHTTPOrHTTPS() &&
      (request.request_type == "main_frame" ||
       request.request_type == "sub_frame" || request.is_third_party);
  engine_host_.Evaluate(
      std::move(request),
      base::BindOnce(
          [](base::WeakPtr<AdBlockService> service,
             std::optional<content::GlobalRenderFrameHostToken> frame_token,
             std::optional<std::string> navigation_url,
             std::string original_url, std::string method,
             AdBlockFactoryType factory_type, bool detrack_eligible,
             DecisionCallback callback,
             AdBlockEngineEvaluationResult result) {
            if (!service) {
              std::move(callback).Run(AdBlockDecision());
              return;
            }
            service->OnEvaluated(
                std::move(frame_token), std::move(navigation_url),
                std::move(original_url), std::move(method), factory_type,
                detrack_eligible, std::move(callback), std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(frame_token),
          std::move(navigation_url), std::move(original_url), std::move(method),
          factory_type, detrack_eligible, std::move(callback)));
}

void AdBlockService::GetCspDirectives(
    AdBlockRequest request,
    const GURL& document_url,
    AdBlockEngineHost::CspDirectivesCallback callback) {
  if (shutdown_ || !document_url.is_valid() ||
      !document_url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(std::string());
    return;
  }
  const AdBlockMode mode =
      settings_.GetSiteSettings(document_url).effective_mode;
  if (mode == AdBlockMode::kOff) {
    std::move(callback).Run(std::string());
    return;
  }
  engine_host_.GetCspDirectives(std::move(request), mode, std::move(callback));
}

void AdBlockService::GetCosmeticResources(const GURL& document_url,
                                          CosmeticResourcesCallback callback) {
  if (shutdown_ || !document_url.is_valid() ||
      !document_url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(AdBlockCosmeticResources());
    return;
  }
  const AdBlockMode mode =
      settings_.GetSiteSettings(document_url).effective_mode;
  if (mode == AdBlockMode::kOff) {
    std::move(callback).Run(AdBlockCosmeticResources());
    return;
  }
  engine_host_.GetCosmeticResources(document_url.spec(), mode,
                                    std::move(callback));
}

void AdBlockService::GetDynamicCosmeticSelectors(
    const GURL& document_url,
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    DynamicCosmeticSelectorsCallback callback) {
  if (shutdown_ || !document_url.is_valid() ||
      !document_url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(AdBlockDynamicCosmeticSelectors());
    return;
  }
  const AdBlockMode mode =
      settings_.GetSiteSettings(document_url).effective_mode;
  if (mode == AdBlockMode::kOff) {
    std::move(callback).Run(AdBlockDynamicCosmeticSelectors());
    return;
  }
  engine_host_.GetDynamicCosmeticSelectors(document_url.spec(), mode,
                                           std::move(classes), std::move(ids),
                                           std::move(callback));
}

void AdBlockService::ReplaceRulesForTesting(
    std::vector<uint8_t> rules,
    AdBlockEngineHost::ReplaceCallback callback) {
  if (shutdown_) {
    std::move(callback).Run(
        AdBlockEngineReplaceResult(false, "service is shut down"));
    return;
  }
  DisableFilterListManagerForTesting();
  engine_host_.ReplaceRules(std::move(rules), std::move(callback));
}

void AdBlockService::ReplaceAdditionalRulesForTesting(
    std::vector<uint8_t> rules,
    AdBlockEngineHost::ReplaceCallback callback) {
  if (shutdown_) {
    std::move(callback).Run(
        AdBlockEngineReplaceResult(false, "service is shut down"));
    return;
  }
  DisableFilterListManagerForTesting();
  engine_host_.ReplaceRules(AdBlockEngineGroup::kAdditional, std::move(rules),
                            std::move(callback));
}

void AdBlockService::ActivateVerifiedFilterComponent(
    const base::FilePath& component_path,
    const base::Version& component_version,
    AdBlockFilterListManager::CompletionCallback callback) {
  if (shutdown_ || !filter_list_manager_) {
    AdBlockFilterListUpdateStatus status;
    status.state = AdBlockFilterListState::kError;
    status.last_error = "filter-list manager is unavailable";
    std::move(callback).Run(std::move(status));
    return;
  }
  filter_list_manager_->ActivateVerifiedComponent(
      component_path, component_version, std::move(callback));
}

void AdBlockService::DownloadPinnedAdditionalRuleSet(
    const GURL& subscription_url,
    std::string expected_sha256,
    const base::Version& rule_set_version,
    AdBlockFilterListManager::CompletionCallback callback) {
  if (shutdown_ || !filter_list_manager_ || !subscription_downloader_) {
    AdBlockFilterListUpdateStatus status;
    status.state = AdBlockFilterListState::kError;
    status.last_error = "filter-list downloader is unavailable";
    std::move(callback).Run(std::move(status));
    return;
  }
  subscription_downloader_->Download(
      subscription_url, std::move(expected_sha256),
      base::BindOnce(&AdBlockService::OnPinnedAdditionalRuleSetDownloaded,
                     weak_factory_.GetWeakPtr(), rule_set_version,
                     std::move(callback)));
}

AdBlockFilterListUpdateStatus AdBlockService::filter_list_status() const {
  return filter_list_manager_ ? filter_list_manager_->status()
                              : AdBlockFilterListUpdateStatus();
}

std::unique_ptr<AdBlockSettings> AdBlockService::SettingsFor(
    content::BrowserContext* context) const {
  Profile* const profile = Profile::FromBrowserContext(context);
  if (!profile) {
    return nullptr;
  }
  return std::make_unique<AdBlockSettings>(
      profile->GetPrefs(), HostContentSettingsMapFactory::GetForProfile(profile));
}

AdBlockSiteSettings AdBlockService::GetSiteSettings(
    const GURL& site_url) const {
  return settings_.GetSiteSettings(site_url);
}

void AdBlockService::SetDefaultMode(AdBlockMode mode) {
  settings_.SetDefaultMode(mode);
}

void AdBlockService::SetSiteMode(const GURL& site_url,
                                 std::optional<AdBlockMode> mode) {
  settings_.SetSiteMode(site_url, mode);
}

FingerprintMode AdBlockService::GetDefaultFingerprintMode() const {
  return settings_.GetDefaultFingerprintMode();
}

void AdBlockService::SetDefaultFingerprintMode(FingerprintMode mode) {
  settings_.SetDefaultFingerprintMode(mode);
}

void AdBlockService::SetSiteFingerprintMode(
    const GURL& site_url,
    std::optional<FingerprintMode> mode) {
  settings_.SetSiteFingerprintMode(site_url, mode);
}

void AdBlockService::SetCanvasFingerprintBlocked(const GURL& site_url,
                                                 bool blocked) {
  settings_.SetCanvasFingerprintBlocked(site_url, blocked);
}

namespace {

// One pattern per registrable domain. Giving sub.example.com a different
// canvas from example.com would not protect either of them; it would only tell
// a tracker which host it was talking to.
std::string SiteKeyFor(const GURL& site_url) {
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          site_url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  // An IP address or an internal hostname has no registrable domain.
  return domain.empty() ? std::string(site_url.host()) : domain;
}

}  // namespace

// static
std::string AdBlockService::IdentityScopeFor(
    const content::BrowserContext* context) {
  // An off-the-record context is served by the original profile's service; its
  // own id keeps its tokens - and so its farbled hashes - unlinkable to the
  // regular profile's, and every new incognito session is a new context.
  return context ? context->UniqueId() : std::string();
}

// static
std::string AdBlockService::IdentityScopeFor(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  std::string scope = IdentityScopeFor(contents->GetBrowserContext());
  content::RenderFrameHost* const frame = contents->GetPrimaryMainFrame();
  content::StoragePartition* const partition =
      frame ? frame->GetStoragePartition() : nullptr;
  if (!partition) {
    return scope;
  }
  // A tab in an isolated Space is pinned to that Space's partition, which is
  // the boundary its storage already respects; the default partition adds
  // nothing to the key, so an ordinary tab's scope is unchanged.
  const content::StoragePartitionConfig& config = partition->GetConfig();
  if (config.is_default()) {
    return scope;
  }
  return scope + "\x1f" + config.partition_domain() + "\x1f" +
         config.partition_name() + (config.in_memory() ? "\x1fm" : "");
}

uint64_t AdBlockService::GetFarblingToken(const GURL& site_url,
                                          const std::string& scope) const {
  const std::string identity_key = scope + "\x1f" + SiteKeyFor(site_url);
  const auto generation = identity_generations_.find(identity_key);
  const uint32_t rotation =
      generation == identity_generations_.end() ? 0u : generation->second;
  const uint64_t hash = base::FastHash(
      base::NumberToString(farbling_session_key_) + "\x1f" + identity_key +
      "\x1f" + base::NumberToString(rotation));
  // The whole 64-bit hash. Folding it to 32 bits, as this once did, threw
  // away exactly the margin that puts the value beyond an offline search.
  // 0 means "farbling off" on the wire, so a real token must never collide
  // with it.
  return hash == 0u ? 1u : hash;
}

void AdBlockService::RotateIdentity(const GURL& site_url,
                                    const std::string& scope) {
  const std::string identity_key = scope + "\x1f" + SiteKeyFor(site_url);
  // Bounded, because the map is keyed by whatever sites a session visits.
  // Evicting the oldest entry would silently re-randomise a site the person
  // deliberately rotated, so a full session's worth is kept and the cap is set
  // where no real session reaches it; past it, only sites that never asked for
  // a rotation are dropped.
  constexpr size_t kMaxRotatedSites = 4096;
  if (identity_generations_.size() >= kMaxRotatedSites &&
      !identity_generations_.contains(identity_key)) {
    identity_generations_.erase(identity_generations_.begin());
  }
  ++identity_generations_[identity_key];
}

SiteIdentity AdBlockService::DescribeIdentity(const GURL& site_url,
                                              const std::string& scope) const {
  SiteIdentity identity;
  const AdBlockSiteSettings settings = GetSiteSettings(site_url);
  identity.farbled = settings.effective_mode != AdBlockMode::kOff &&
                     settings.fingerprint_mode != FingerprintMode::kOff;
  identity.token = identity.farbled ? GetFarblingToken(site_url, scope) : 0u;
  identity.real_cores = base::SysInfo::NumberOfProcessors();
  blink::ApproximatedDeviceMemory::Initialize();
  // The specification's clamped power-of-two bucket, not physical RAM.
  identity.memory_class_gib =
      blink::ApproximatedDeviceMemory::GetApproximatedDeviceMemory();
  // The very functions the renderer runs, on the very token it holds.
  identity.reported_cores = blink::SeoulFarbleHardwareConcurrency(
      static_cast<unsigned>(std::max(identity.real_cores, 0)), identity.token);
  identity.reported_memory_gib = blink::SeoulFarbleDeviceMemory(
      identity.memory_class_gib, identity.token);
  if (identity.farbled) {
    // Four hex digits folded from the whole token, enough for a person to
    // tell two patterns apart and far too few to narrow the token itself.
    const uint64_t folded = identity.token ^ (identity.token >> 32);
    identity.persona = base::StringPrintf(
        "%04X",
        static_cast<unsigned>((folded ^ (folded >> 16)) & 0xFFFFu));
  }
  return identity;
}

void AdBlockService::TemporarilyDisable(const GURL& site_url,
                                        base::TimeDelta duration) {
  settings_.TemporarilyDisable(site_url, duration);
}

void AdBlockService::ClearTemporaryDisable(const GURL& site_url) {
  settings_.ClearTemporaryDisable(site_url);
}

base::WeakPtr<AdBlockService> AdBlockService::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void AdBlockService::Shutdown() {
  shutdown_ = true;
  catalogue_subscriber_.reset();
  subscription_downloader_.reset();
  catalogue_downloader_.reset();
  if (filter_list_manager_) {
    filter_list_manager_->Shutdown();
  }
  weak_factory_.InvalidateWeakPtrs();
  request_interceptors_.clear();
}

void AdBlockService::AddRequestInterceptor(
    std::unique_ptr<AdBlockRequestInterceptor> interceptor) {
  request_interceptors_.insert(std::move(interceptor));
}

void AdBlockService::RemoveRequestInterceptor(
    AdBlockRequestInterceptor* interceptor) {
  const auto it = request_interceptors_.find(interceptor);
  if (it != request_interceptors_.end()) {
    request_interceptors_.erase(it);
  }
}

void AdBlockService::DisableFilterListManagerForTesting() {
  if (!filter_list_manager_) {
    return;
  }
  catalogue_subscriber_.reset();
  subscription_downloader_.reset();
  catalogue_downloader_.reset();
  filter_list_manager_->Shutdown();
  filter_list_manager_.reset();
}

void AdBlockService::OnPinnedAdditionalRuleSetDownloaded(
    base::Version rule_set_version,
    AdBlockFilterListManager::CompletionCallback callback,
    AdBlockSubscriptionDownloadResult result) {
  if (!filter_list_manager_) {
    AdBlockFilterListUpdateStatus status;
    status.state = AdBlockFilterListState::kError;
    status.last_error = "filter-list manager is unavailable";
    std::move(callback).Run(std::move(status));
    return;
  }
  if (!result.success) {
    filter_list_manager_->ReportUpdateFailure(std::move(result.error),
                                              std::move(callback));
    return;
  }
  filter_list_manager_->ActivatePinnedAdditionalRuleSet(
      std::move(result.rules), rule_set_version, std::move(callback));
}

void AdBlockService::OnEvaluated(
    std::optional<content::GlobalRenderFrameHostToken> frame_token,
    std::optional<std::string> navigation_url,
    std::string original_url,
    std::string method,
    AdBlockFactoryType factory_type,
    DecisionCallback callback,
    AdBlockEngineEvaluationResult result) {
  AdBlockDecision decision;
  decision.deciding_engine = result.deciding_engine;
  decision.matched = result.match.matched;
  decision.important = result.match.important;
  decision.has_exception = result.match.has_exception;
  decision.matched_rule = std::move(result.match.matched_rule);
  decision.exception_rule = std::move(result.match.exception_rule);
  decision.redirect = std::move(result.match.redirect);
  decision.rewritten_url = std::move(result.match.rewritten_url);
  const bool should_block =
      decision.important || (decision.matched && !decision.has_exception);
  // A replacement resource is only servable by the subresource interceptor,
  // which synthesises the response. Navigations have no such path, so keying
  // this on the factory type rather than on `navigation_url` matters: a
  // subframe navigation carries no navigation_url, and would otherwise be
  // marked kRedirect and then let straight through by the throttle.
  const bool servable_as_replacement =
      factory_type != AdBlockFactoryType::kNavigation &&
      factory_type != AdBlockFactoryType::kWebSocket;
  if (should_block && servable_as_replacement && decision.redirect &&
      FindAdBlockResourceByDataUrl(*decision.redirect)) {
    decision.action = AdBlockAction::kRedirect;
    decision.rule_category = AdBlockRuleCategory::kRedirect;
  } else if (should_block) {
    decision.action = AdBlockAction::kBlock;
    decision.rule_category = decision.important
                                 ? AdBlockRuleCategory::kImportant
                                 : AdBlockRuleCategory::kNetwork;
  } else if (decision.rewritten_url) {
    const GURL rewritten_url(*decision.rewritten_url);
    if (IsSafeAdBlockUrlRewrite(GURL(original_url), rewritten_url, method)) {
      decision.action = AdBlockAction::kRewrite;
      decision.rule_category = AdBlockRuleCategory::kRewrite;
    } else {
      decision.rewritten_url.reset();
    }
  } else if (decision.has_exception) {
    decision.rule_category = AdBlockRuleCategory::kException;
  }
  if (decision.action == AdBlockAction::kBlock ||
      decision.action == AdBlockAction::kRedirect) {
    stats_.RecordBlocked(frame_token);
    if (decision.action == AdBlockAction::kBlock && navigation_url) {
      last_blocked_navigation_ =
          AdBlockBlockedNavigation{std::move(*navigation_url), decision};
    }
  }
  std::move(callback).Run(std::move(decision));
}

}  // namespace seoul::adblock
