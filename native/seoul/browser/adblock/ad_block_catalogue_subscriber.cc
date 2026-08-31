// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/adblock/ad_block_catalogue_subscriber.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace seoul::adblock {

namespace {

// A round that cannot complete within this is abandoned and retried on the next
// interval. The downloader already bounds each individual request; this bounds
// the sequence, so a source that accepts a connection and then stalls forever
// cannot pin the coordinator open and block every later round.
constexpr base::TimeDelta kMinimumRefreshInterval = base::Hours(1);

}  // namespace

AdBlockCatalogueSubscriber::AdBlockCatalogueSubscriber(
    Fetcher fetcher,
    InstallCallback install,
    std::vector<std::string> profile_languages)
    : fetcher_(std::move(fetcher)),
      languages_(NormalizeCatalogLanguages(profile_languages)),
      install_(std::move(install)) {}

AdBlockCatalogueSubscriber::~AdBlockCatalogueSubscriber() = default;

// static
std::vector<AdBlockCatalogEntry>
AdBlockCatalogueSubscriber::SelectSubscribedEntries(
    const std::vector<AdBlockCatalogEntry>& catalog,
    const std::vector<std::string>& normalized_languages) {
  std::vector<AdBlockCatalogEntry> selected;
  for (const AdBlockCatalogEntry& entry : catalog) {
    // A regional list is selected by language match; everything else by its
    // default flag. Language never widens a global list's selection and a
    // default-enabled list never needs a language.
    const bool language_selected =
        !entry.languages.empty() &&
        std::ranges::any_of(entry.languages, [&](const std::string& lang) {
          return std::ranges::find(normalized_languages, lang) !=
                 normalized_languages.end();
        });
    if (!entry.enabled_by_default && !language_selected) {
      continue;
    }
    if (entry.delivery != AdBlockListDelivery::kRuntimeDownload) {
      continue;
    }
    // Bundled entries carry no url, and a catalogued url that is not HTTPS is
    // not fetched at all rather than downgraded.
    const GURL url(entry.url);
    if (!url.is_valid() || !url.SchemeIs("https")) {
      continue;
    }
    selected.push_back(entry);
  }
  return selected;
}

// static
base::TimeDelta AdBlockCatalogueSubscriber::RefreshIntervalFor(
    const std::vector<AdBlockCatalogEntry>& entries) {
  base::TimeDelta interval = base::Hours(24);
  for (const AdBlockCatalogEntry& entry : entries) {
    if (entry.update_interval_hours > 0) {
      interval = std::min(interval, base::Hours(entry.update_interval_hours));
    }
  }
  return std::max(interval, kMinimumRefreshInterval);
}

void AdBlockCatalogueSubscriber::Start() {
  if (round_running_) {
    return;
  }
  RunRound();
}

void AdBlockCatalogueSubscriber::RunRound() {
  entries_ = SelectSubscribedEntries(GetAdBlockFilterCatalog(), languages_);
  if (entries_.empty()) {
    // Nothing catalogued for runtime delivery. Not an error: the bundled
    // baseline is a complete, valid ruleset on its own.
    ScheduleNextRound();
    return;
  }
  round_running_ = true;
  next_index_ = 0;
  collected_.clear();
  collected_.reserve(entries_.size());
  FetchNext();
}

void AdBlockCatalogueSubscriber::FetchNext() {
  if (next_index_ >= entries_.size()) {
    FinishRound(/*success=*/true, std::string());
    return;
  }
  const AdBlockCatalogEntry& entry = entries_[next_index_];
  fetcher_.Run(entry,
               base::BindOnce(&AdBlockCatalogueSubscriber::OnFetched,
                              weak_factory_.GetWeakPtr()));
}

void AdBlockCatalogueSubscriber::OnFetched(bool success,
                                           std::string rules,
                                           std::string error) {
  if (success) {
    collected_.push_back(std::move(rules));
  } else {
    // Skip this list and keep going.
    //
    // Abandoning the round on one failure was defensible when the catalogue
    // held a handful of lists, on the reasoning that installing part of a set
    // narrows protection without saying so. With a Brave-sized catalogue that
    // reasoning inverts: one unreachable host would install NOTHING and leave
    // the profile on the bundled baseline alone, which narrows protection far
    // more than missing one list. So the round continues and every list that
    // failed is named in the status, which answers the original objection -
    // the narrowing is reported rather than silent.
    if (failures_.size() < kMaxReportedFailures) {
      failures_.push_back(base::StrCat({entries_[next_index_].id, ": ", error}));
    }
  }
  ++next_index_;
  FetchNext();
}

void AdBlockCatalogueSubscriber::FinishRound(bool success, std::string error) {
  round_running_ = false;
  last_error_ = std::move(error);
  if (!success) {
    ScheduleNextRound();
    return;
  }

  std::string combined;
  size_t total = 0;
  for (const std::string& rules : collected_) {
    total += rules.size() + 1;
  }
  combined.reserve(total);
  for (std::string& rules : collected_) {
    combined.append(rules);
    // Lists do not reliably end in a newline, and concatenating without one
    // would splice the last rule of one list onto the first of the next.
    combined.append("\n");
  }
  collected_.clear();

  ++completed_rounds_;
  install_.Run(std::move(combined),
               base::BindOnce(&AdBlockCatalogueSubscriber::ScheduleNextRound,
                              weak_factory_.GetWeakPtr()));
}

void AdBlockCatalogueSubscriber::ScheduleNextRound() {
  refresh_timer_.Start(FROM_HERE, RefreshIntervalFor(entries_),
                       base::BindOnce(&AdBlockCatalogueSubscriber::RunRound,
                                      weak_factory_.GetWeakPtr()));
}

}  // namespace seoul::adblock
