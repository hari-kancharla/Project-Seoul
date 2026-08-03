// Project Seoul verified filter-list activation and last-known-good storage.

#include "seoul/browser/adblock/ad_block_filter_list_manager.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "crypto/sha2.h"

namespace seoul::adblock {

struct AdBlockFilterListManager::PackageLoadResult {
  PackageLoadResult();
  PackageLoadResult(const PackageLoadResult&);
  PackageLoadResult& operator=(const PackageLoadResult&);
  PackageLoadResult(PackageLoadResult&&);
  PackageLoadResult& operator=(PackageLoadResult&&);
  ~PackageLoadResult();

  bool success = false;
  std::string version;
  std::string default_rules;
  std::string additional_rules;
  std::string warning;
  std::string error;
};

AdBlockFilterListManager::PackageLoadResult::PackageLoadResult() = default;
AdBlockFilterListManager::PackageLoadResult::PackageLoadResult(
    const PackageLoadResult&) = default;
AdBlockFilterListManager::PackageLoadResult&
AdBlockFilterListManager::PackageLoadResult::operator=(
    const PackageLoadResult&) = default;
AdBlockFilterListManager::PackageLoadResult::PackageLoadResult(
    PackageLoadResult&&) = default;
AdBlockFilterListManager::PackageLoadResult&
AdBlockFilterListManager::PackageLoadResult::operator=(PackageLoadResult&&) =
    default;
AdBlockFilterListManager::PackageLoadResult::~PackageLoadResult() = default;

namespace {

constexpr char kCacheDirectory[] = "SeoulAdBlock";
constexpr char kActiveSlotFile[] = "active-slot";
constexpr char kManifestFile[] = "manifest.json";
constexpr char kDefaultRulesFile[] = "default.txt";
constexpr char kAdditionalRulesFile[] = "additional.txt";
constexpr char kSlotA[] = "a";
constexpr char kSlotB[] = "b";
constexpr char kPackageFormat[] = "seoul-adblock-filter-set";
constexpr int kPackageSchemaVersion = 1;
constexpr size_t kMaxManifestBytes = 64 * 1024;
constexpr size_t kMaxRulesBytes = 32 * 1024 * 1024;
constexpr size_t kMaxCombinedRulesBytes = 48 * 1024 * 1024;

constexpr char kBundledVersion[] = "0.1.0";
constexpr char kBundledDefaultRules[] =
    "! Seoul safety baseline. Production lists arrive through a verified "
    "component.\n"
    "||seoul-adblock.invalid^\n";
constexpr char kBundledAdditionalRules[] =
    "! Seoul user/additional rule baseline.\n";

constexpr char kStatePref[] = "seoul.adblock.filter_lists.state";
constexpr char kSourcePref[] = "seoul.adblock.filter_lists.source";
constexpr char kVersionPref[] = "seoul.adblock.filter_lists.version";
constexpr char kLastErrorPref[] = "seoul.adblock.filter_lists.last_error";
constexpr char kLastAttemptPref[] = "seoul.adblock.filter_lists.last_attempt";
constexpr char kLastSuccessPref[] = "seoul.adblock.filter_lists.last_success";

std::string Sha256Hex(std::string_view contents) {
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(contents)));
}

bool ReadBoundedFile(const base::FilePath& path,
                     size_t maximum_size,
                     std::string* contents,
                     std::string* error) {
  const std::optional<int64_t> size = base::GetFileSize(path);
  if (!size) {
    *error = "missing file: " + path.BaseName().AsUTF8Unsafe();
    return false;
  }
  if (*size < 0 || static_cast<uint64_t>(*size) > maximum_size) {
    *error = "oversized file: " + path.BaseName().AsUTF8Unsafe();
    return false;
  }
  if (!base::ReadFileToStringWithMaxSize(path, contents, maximum_size)) {
    *error = "unreadable file: " + path.BaseName().AsUTF8Unsafe();
    return false;
  }
  return true;
}

bool ValidateRuleText(std::string_view rules,
                      bool allow_empty,
                      std::string_view label,
                      std::string* error) {
  if (!allow_empty && rules.empty()) {
    *error = std::string(label) + " rules are empty";
    return false;
  }
  if (rules.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(rules)) {
    *error = std::string(label) + " rules are not valid UTF-8 text";
    return false;
  }
  return true;
}

AdBlockFilterListManager::PackageLoadResult MakeBundledPackage();
AdBlockFilterListManager::PackageLoadResult ReadPackage(
    const base::FilePath& package_path,
    const std::optional<base::Version>& expected_version);

AdBlockFilterListManager::PackageLoadResult ReadCachedPackage(
    const base::FilePath& cache_path) {
  if (!base::PathExists(cache_path.AppendASCII(kActiveSlotFile))) {
    return AdBlockFilterListManager::PackageLoadResult();
  }
  std::string active_slot;
  std::string marker_error;
  if (!ReadBoundedFile(cache_path.AppendASCII(kActiveSlotFile), 8, &active_slot,
                       &marker_error)) {
    AdBlockFilterListManager::PackageLoadResult result;
    result.error = std::move(marker_error);
    return result;
  }
  base::TrimWhitespaceASCII(active_slot, base::TRIM_ALL, &active_slot);
  if (active_slot != kSlotA && active_slot != kSlotB) {
    AdBlockFilterListManager::PackageLoadResult result;
    result.error = "invalid active cache slot";
    return result;
  }

  const char* fallback_slot = active_slot == kSlotA ? kSlotB : kSlotA;
  AdBlockFilterListManager::PackageLoadResult active =
      ReadPackage(cache_path.AppendASCII(active_slot), std::nullopt);
  if (active.success) {
    return active;
  }
  AdBlockFilterListManager::PackageLoadResult fallback =
      ReadPackage(cache_path.AppendASCII(fallback_slot), std::nullopt);
  if (fallback.success) {
    fallback.warning = "active cache slot invalid: " + active.error;
    return fallback;
  }
  active.error += "; fallback cache slot invalid: " + fallback.error;
  return active;
}

std::string PersistPackage(const base::FilePath& cache_path,
                           const std::string& version,
                           const std::string& default_rules,
                           const std::string& additional_rules) {
  if (!base::CreateDirectory(cache_path)) {
    return "could not create filter-list cache";
  }

  std::string active_slot;
  base::ReadFileToStringWithMaxSize(cache_path.AppendASCII(kActiveSlotFile),
                                    &active_slot, 8);
  base::TrimWhitespaceASCII(active_slot, base::TRIM_ALL, &active_slot);
  const std::string target_slot = active_slot == kSlotA ? kSlotB : kSlotA;
  const base::FilePath target_path = cache_path.AppendASCII(target_slot);
  if (!base::CreateDirectory(target_path)) {
    return "could not create filter-list cache slot";
  }

  base::DictValue manifest;
  manifest.Set("format", kPackageFormat);
  manifest.Set("schema_version", kPackageSchemaVersion);
  manifest.Set("version", version);
  manifest.Set("default_sha256", Sha256Hex(default_rules));
  manifest.Set("additional_sha256", Sha256Hex(additional_rules));
  std::string manifest_json;
  if (!base::JSONWriter::Write(manifest, &manifest_json)) {
    return "could not serialize filter-list manifest";
  }

  if (!base::ImportantFileWriter::WriteFileAtomically(
          target_path.AppendASCII(kDefaultRulesFile), default_rules,
          "SeoulAdBlock") ||
      !base::ImportantFileWriter::WriteFileAtomically(
          target_path.AppendASCII(kAdditionalRulesFile), additional_rules,
          "SeoulAdBlock") ||
      !base::ImportantFileWriter::WriteFileAtomically(
          target_path.AppendASCII(kManifestFile), manifest_json,
          "SeoulAdBlock")) {
    return "could not atomically write filter-list cache slot";
  }

  // Commit the new slot only after every hashed payload has landed. The old
  // slot remains available as a fallback if the marker or new slot is damaged.
  if (!base::ImportantFileWriter::WriteFileAtomically(
          cache_path.AppendASCII(kActiveSlotFile), target_slot,
          "SeoulAdBlock")) {
    return "could not commit filter-list cache slot";
  }
  return std::string();
}

}  // namespace

namespace {

AdBlockFilterListManager::PackageLoadResult MakeBundledPackage() {
  AdBlockFilterListManager::PackageLoadResult result;
  result.success = true;
  result.version = kBundledVersion;
  result.default_rules = kBundledDefaultRules;
  result.additional_rules = kBundledAdditionalRules;
  return result;
}

AdBlockFilterListManager::PackageLoadResult ReadPackage(
    const base::FilePath& package_path,
    const std::optional<base::Version>& expected_version) {
  AdBlockFilterListManager::PackageLoadResult result;
  std::string manifest_json;
  if (!ReadBoundedFile(package_path.AppendASCII(kManifestFile),
                       kMaxManifestBytes, &manifest_json, &result.error)) {
    return result;
  }

  std::optional<base::Value> manifest =
      base::JSONReader::Read(manifest_json, /*options=*/0, /*max_depth=*/16);
  if (!manifest || !manifest->is_dict()) {
    result.error = "invalid filter-list manifest JSON";
    return result;
  }
  const base::DictValue& dict = manifest->GetDict();
  const std::string* format = dict.FindString("format");
  const std::optional<int> schema_version = dict.FindInt("schema_version");
  const std::string* version = dict.FindString("version");
  const std::string* default_hash = dict.FindString("default_sha256");
  const std::string* additional_hash = dict.FindString("additional_sha256");
  if (!format || *format != kPackageFormat || !schema_version ||
      *schema_version != kPackageSchemaVersion || !version || !default_hash ||
      !additional_hash) {
    result.error = "filter-list manifest has missing or unsupported fields";
    return result;
  }

  const base::Version parsed_version(*version);
  if (!parsed_version.IsValid() ||
      (expected_version && parsed_version != *expected_version)) {
    result.error = "filter-list manifest version mismatch";
    return result;
  }
  if (default_hash->size() != crypto::kSHA256Length * 2 ||
      additional_hash->size() != crypto::kSHA256Length * 2) {
    result.error = "filter-list manifest hash has invalid length";
    return result;
  }

  if (!ReadBoundedFile(package_path.AppendASCII(kDefaultRulesFile),
                       kMaxRulesBytes, &result.default_rules, &result.error) ||
      !ReadBoundedFile(package_path.AppendASCII(kAdditionalRulesFile),
                       kMaxRulesBytes, &result.additional_rules,
                       &result.error)) {
    return result;
  }
  if (result.default_rules.size() + result.additional_rules.size() >
      kMaxCombinedRulesBytes) {
    result.error = "combined filter-list payload is oversized";
    return result;
  }
  if (!ValidateRuleText(result.default_rules, false, "default",
                        &result.error) ||
      !ValidateRuleText(result.additional_rules, true, "additional",
                        &result.error)) {
    return result;
  }
  if (!base::EqualsCaseInsensitiveASCII(*default_hash,
                                        Sha256Hex(result.default_rules)) ||
      !base::EqualsCaseInsensitiveASCII(*additional_hash,
                                        Sha256Hex(result.additional_rules))) {
    result.error = "filter-list payload hash mismatch";
    return result;
  }

  result.success = true;
  result.version = *version;
  return result;
}

}  // namespace

AdBlockFilterListUpdateStatus::AdBlockFilterListUpdateStatus() = default;
AdBlockFilterListUpdateStatus::AdBlockFilterListUpdateStatus(
    const AdBlockFilterListUpdateStatus&) = default;
AdBlockFilterListUpdateStatus& AdBlockFilterListUpdateStatus::operator=(
    const AdBlockFilterListUpdateStatus&) = default;
AdBlockFilterListUpdateStatus::AdBlockFilterListUpdateStatus(
    AdBlockFilterListUpdateStatus&&) = default;
AdBlockFilterListUpdateStatus& AdBlockFilterListUpdateStatus::operator=(
    AdBlockFilterListUpdateStatus&&) = default;
AdBlockFilterListUpdateStatus::~AdBlockFilterListUpdateStatus() = default;

AdBlockFilterListManager::AdBlockFilterListManager(
    AdBlockEngineHost* engine_host,
    base::FilePath profile_path,
    PrefService* prefs)
    : engine_host_(engine_host),
      cache_path_(profile_path.AppendASCII(kCacheDirectory)),
      prefs_(prefs) {}

AdBlockFilterListManager::~AdBlockFilterListManager() = default;

// static
void AdBlockFilterListManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(
      kStatePref, static_cast<int>(AdBlockFilterListState::kNotStarted));
  registry->RegisterIntegerPref(
      kSourcePref, static_cast<int>(AdBlockFilterListSource::kNone));
  registry->RegisterStringPref(kVersionPref, std::string());
  registry->RegisterStringPref(kLastErrorPref, std::string());
  registry->RegisterTimePref(kLastAttemptPref, base::Time());
  registry->RegisterTimePref(kLastSuccessPref, base::Time());
}

void AdBlockFilterListManager::Start(CompletionCallback callback) {
  const uint64_t generation = BeginAttempt();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadCachedPackage, cache_path_),
      base::BindOnce(&AdBlockFilterListManager::OnStartupPackageRead,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

void AdBlockFilterListManager::ActivateVerifiedComponent(
    const base::FilePath& component_path,
    const base::Version& component_version,
    CompletionCallback callback) {
  const uint64_t generation = BeginAttempt();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadPackage, component_path,
                     std::optional<base::Version>(component_version)),
      base::BindOnce(&AdBlockFilterListManager::OnVerifiedPackageRead,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

void AdBlockFilterListManager::ActivatePinnedAdditionalRuleSet(
    std::string rules,
    const base::Version& rule_set_version,
    CompletionCallback callback) {
  const uint64_t generation = BeginAttempt();
  PackageLoadResult package;
  if (!rule_set_version.IsValid()) {
    CompleteFailure(generation, "pinned rule-set version is invalid",
                    std::move(callback));
    return;
  }
  if (!ValidateRuleText(rules, false, "pinned subscription", &package.error)) {
    CompleteFailure(generation, std::move(package.error), std::move(callback));
    return;
  }
  package.success = true;
  package.version = rule_set_version.GetString();
  package.default_rules = active_default_rules_.empty()
                              ? std::string(kBundledDefaultRules)
                              : active_default_rules_;
  package.additional_rules = std::move(rules);
  ActivatePackage(generation, AdBlockFilterListSource::kPinnedSubscription,
                  true, std::string(), std::move(callback), std::move(package));
}

void AdBlockFilterListManager::ReportUpdateFailure(
    std::string error,
    CompletionCallback callback) {
  const uint64_t generation = BeginAttempt();
  CompleteFailure(generation, std::move(error), std::move(callback));
}

void AdBlockFilterListManager::Shutdown() {
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
  ++generation_;
  weak_factory_.InvalidateWeakPtrs();
}

uint64_t AdBlockFilterListManager::BeginAttempt() {
  ++generation_;
  status_.state = AdBlockFilterListState::kLoading;
  status_.last_attempt = base::Time::Now();
  status_.last_error.clear();
  PersistStatus();
  return generation_;
}

void AdBlockFilterListManager::OnStartupPackageRead(uint64_t generation,
                                                    CompletionCallback callback,
                                                    PackageLoadResult result) {
  if (generation != generation_) {
    CompleteSuperseded(std::move(callback));
    return;
  }
  if (result.success) {
    ActivatePackage(generation, AdBlockFilterListSource::kCache, false,
                    std::move(result.warning), std::move(callback),
                    std::move(result));
    return;
  }

  std::string warning = std::move(result.error);
  ActivatePackage(generation, AdBlockFilterListSource::kBundled, false,
                  std::move(warning), std::move(callback),
                  MakeBundledPackage());
}

void AdBlockFilterListManager::OnVerifiedPackageRead(
    uint64_t generation,
    CompletionCallback callback,
    PackageLoadResult result) {
  if (generation != generation_) {
    CompleteSuperseded(std::move(callback));
    return;
  }
  if (!result.success) {
    CompleteFailure(generation, std::move(result.error), std::move(callback));
    return;
  }
  ActivatePackage(generation, AdBlockFilterListSource::kVerifiedComponent, true,
                  std::string(), std::move(callback), std::move(result));
}

void AdBlockFilterListManager::ActivatePackage(uint64_t generation,
                                               AdBlockFilterListSource source,
                                               bool persist_on_success,
                                               std::string prior_warning,
                                               CompletionCallback callback,
                                               PackageLoadResult package) {
  std::vector<uint8_t> default_rules(package.default_rules.begin(),
                                     package.default_rules.end());
  std::vector<uint8_t> additional_rules(package.additional_rules.begin(),
                                        package.additional_rules.end());
  engine_host_->ReplaceRuleSets(
      std::move(default_rules), std::move(additional_rules),
      base::BindOnce(&AdBlockFilterListManager::OnPackageActivated,
                     weak_factory_.GetWeakPtr(), generation, source,
                     persist_on_success, std::move(prior_warning),
                     std::move(callback), std::move(package)));
}

void AdBlockFilterListManager::OnPackageActivated(
    uint64_t generation,
    AdBlockFilterListSource source,
    bool persist_on_success,
    std::string prior_warning,
    CompletionCallback callback,
    PackageLoadResult package,
    AdBlockEngineReplaceResult result) {
  if (generation != generation_) {
    CompleteSuperseded(std::move(callback));
    return;
  }
  if (!result.success) {
    CompleteFailure(generation, "rule engine rejected package: " + result.error,
                    std::move(callback));
    return;
  }

  status_.state = AdBlockFilterListState::kReady;
  status_.source = source;
  status_.version = package.version;
  status_.last_success = base::Time::Now();
  status_.last_error = std::move(prior_warning);
  active_default_rules_ = package.default_rules;
  PersistStatus();

  if (!persist_on_success) {
    std::move(callback).Run(status_);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&PersistPackage, cache_path_, package.version,
                     std::move(package.default_rules),
                     std::move(package.additional_rules)),
      base::BindOnce(&AdBlockFilterListManager::OnPackagePersisted,
                     weak_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

void AdBlockFilterListManager::OnPackagePersisted(
    uint64_t generation,
    CompletionCallback callback,
    std::string persistence_error) {
  if (generation != generation_) {
    CompleteSuperseded(std::move(callback));
    return;
  }
  status_.last_error = std::move(persistence_error);
  PersistStatus();
  std::move(callback).Run(status_);
}

void AdBlockFilterListManager::CompleteFailure(uint64_t generation,
                                               std::string error,
                                               CompletionCallback callback) {
  if (generation != generation_) {
    CompleteSuperseded(std::move(callback));
    return;
  }
  // Failure never changes the last successfully activated source/version.
  status_.state = AdBlockFilterListState::kError;
  status_.last_error = std::move(error);
  PersistStatus();
  std::move(callback).Run(status_);
}

void AdBlockFilterListManager::CompleteSuperseded(CompletionCallback callback) {
  if (callback) {
    std::move(callback).Run(status_);
  }
}

void AdBlockFilterListManager::PersistStatus() {
  if (!prefs_) {
    return;
  }
  prefs_->SetInteger(kStatePref, static_cast<int>(status_.state));
  prefs_->SetInteger(kSourcePref, static_cast<int>(status_.source));
  prefs_->SetString(kVersionPref, status_.version);
  prefs_->SetString(kLastErrorPref, status_.last_error);
  prefs_->SetTime(kLastAttemptPref, status_.last_attempt);
  prefs_->SetTime(kLastSuccessPref, status_.last_success);
}

}  // namespace seoul::adblock
