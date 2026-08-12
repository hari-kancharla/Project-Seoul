// Project Seoul verified filter-list activation and last-known-good storage.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_LIST_MANAGER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_LIST_MANAGER_H_

#include <cstdint>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/version.h"
#include "seoul/browser/adblock/ad_block_engine_host.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace seoul::adblock {

enum class AdBlockFilterListState {
  kNotStarted = 0,
  kLoading = 1,
  kReady = 2,
  kError = 3,
};

enum class AdBlockFilterListSource {
  kNone = 0,
  kBundled = 1,
  kCache = 2,
  kVerifiedComponent = 3,
  kPinnedSubscription = 4,
  kCatalogueSubscription = 5,
};

struct AdBlockFilterListUpdateStatus {
  AdBlockFilterListUpdateStatus();
  AdBlockFilterListUpdateStatus(const AdBlockFilterListUpdateStatus&);
  AdBlockFilterListUpdateStatus& operator=(
      const AdBlockFilterListUpdateStatus&);
  AdBlockFilterListUpdateStatus(AdBlockFilterListUpdateStatus&&);
  AdBlockFilterListUpdateStatus& operator=(AdBlockFilterListUpdateStatus&&);
  ~AdBlockFilterListUpdateStatus();

  AdBlockFilterListState state = AdBlockFilterListState::kNotStarted;
  AdBlockFilterListSource source = AdBlockFilterListSource::kNone;
  std::string version;
  std::string last_error;
  base::Time last_attempt;
  base::Time last_success;
};

// Accepts already signature-verified component contents, validates the inner
// package and both rule sets, then activates them as one transaction. The
// profile cache uses two slots plus an atomically written active-slot marker so
// an interrupted write never destroys the previous known-good package.
class AdBlockFilterListManager {
 public:
  using CompletionCallback =
      base::OnceCallback<void(AdBlockFilterListUpdateStatus)>;
  // Internal transfer object is public only so file-reading helpers can return
  // it without exposing those helpers as part of the manager API.
  struct PackageLoadResult;

  AdBlockFilterListManager(AdBlockEngineHost* engine_host,
                           base::FilePath profile_path,
                           PrefService* prefs);
  ~AdBlockFilterListManager();

  AdBlockFilterListManager(const AdBlockFilterListManager&) = delete;
  AdBlockFilterListManager& operator=(const AdBlockFilterListManager&) = delete;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Loads the newest valid last-known-good cache without waiting for network.
  // A missing or invalid cache falls back to Seoul's bundled safety baseline.
  void Start(CompletionCallback callback);

  // `component_version` comes from Chromium's CRX-verified component metadata.
  // The inner manifest must repeat that exact version and hash both list files.
  void ActivateVerifiedComponent(const base::FilePath& component_path,
                                 const base::Version& component_version,
                                 CompletionCallback callback);
  void ActivatePinnedAdditionalRuleSet(std::string rules,
                                       const base::Version& rule_set_version,
                                       CompletionCallback callback);

  // Installs catalogued upstream lists into the DEFAULT engine, appended after
  // the Seoul baseline rather than replacing it, so the baseline stays a floor
  // even when an upstream fetch has just succeeded. Separate from the pinned
  // path because the catalogued lists are group kDefault: routing them through
  // the additional engine would quietly give them the URL-rewrite capability
  // that the two-engine policy reserves for user and optional lists.
  void ActivateCatalogueDefaultLists(std::string rules,
                                     const base::Version& list_version,
                                     CompletionCallback callback);
  void ReportUpdateFailure(std::string error, CompletionCallback callback);

  const AdBlockFilterListUpdateStatus& status() const { return status_; }
  void Shutdown();

 private:
  uint64_t BeginAttempt();
  void OnStartupPackageRead(uint64_t generation,
                            CompletionCallback callback,
                            PackageLoadResult result);
  void OnVerifiedPackageRead(uint64_t generation,
                             CompletionCallback callback,
                             PackageLoadResult result);
  void ActivatePackage(uint64_t generation,
                       AdBlockFilterListSource source,
                       bool persist_on_success,
                       std::string prior_warning,
                       CompletionCallback callback,
                       PackageLoadResult package);
  void OnPackageActivated(uint64_t generation,
                          AdBlockFilterListSource source,
                          bool persist_on_success,
                          std::string prior_warning,
                          CompletionCallback callback,
                          PackageLoadResult package,
                          AdBlockEngineReplaceResult result);
  void OnPackagePersisted(uint64_t generation,
                          CompletionCallback callback,
                          std::string persistence_error);
  void CompleteFailure(uint64_t generation,
                       std::string error,
                       CompletionCallback callback);
  void CompleteSuperseded(CompletionCallback callback);
  void PersistStatus();

  const raw_ptr<AdBlockEngineHost> engine_host_;
  const base::FilePath cache_path_;
  const raw_ptr<PrefService> prefs_;
  bool shutdown_ = false;
  uint64_t generation_ = 0;
  AdBlockFilterListUpdateStatus status_;
  std::string active_default_rules_;
  base::WeakPtrFactory<AdBlockFilterListManager> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_FILTER_LIST_MANAGER_H_
