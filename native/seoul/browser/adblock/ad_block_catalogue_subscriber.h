// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CATALOGUE_SUBSCRIBER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CATALOGUE_SUBSCRIBER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "seoul/browser/adblock/ad_block_filter_catalog.h"

namespace seoul::adblock {

// Fetches the catalogued upstream filter lists and hands them to the filter
// list manager.
//
// The catalog has always declared EasyList and EasyPrivacy as
// `enabled_by_default` runtime downloads, and nothing read that declaration, so
// the browser ran on its bundled baseline alone and let the ad-serving class
// straight through. This is the missing half: it turns the catalog from a
// description into the thing that actually happens.
//
// Design notes that matter:
//
//   - One request outstanding at a time. The downloader holds a single
//     SimpleURLLoader, so entries are fetched in sequence rather than
//     concurrently. Filter lists are a few megabytes and fetched once a day;
//     the simpler lifetime is worth more here than the parallelism.
//   - All-or-nothing per round. Partial results are not installed. A round that
//     loses one list to a network error would otherwise silently narrow
//     protection, and the user has no way to see that happened.
//   - Failure changes nothing. If a round fails, no activation is attempted, so
//     whatever engine is live stays live - the previous lists, the cache, or
//     the baseline. The next refresh tries again.
//   - The floor is never removed. The manager appends these lists after the
//     Seoul baseline rather than replacing it.
class AdBlockCatalogueSubscriber {
 public:
  // Downloads one list. `on_done` receives the rules on success, or an empty
  // optional with `error` set. Injected so the coordinator is testable without
  // a network stack.
  using FetchCallback =
      base::OnceCallback<void(bool success, std::string rules, std::string error)>;
  using Fetcher =
      base::RepeatingCallback<void(const AdBlockCatalogEntry&, FetchCallback)>;

  // Receives the concatenated rules of a complete, successful round.
  using InstallCallback =
      base::RepeatingCallback<void(std::string rules, base::OnceClosure done)>;

  AdBlockCatalogueSubscriber(Fetcher fetcher, InstallCallback install);
  ~AdBlockCatalogueSubscriber();

  AdBlockCatalogueSubscriber(const AdBlockCatalogueSubscriber&) = delete;
  AdBlockCatalogueSubscriber& operator=(const AdBlockCatalogueSubscriber&) =
      delete;

  // Runs a round now and schedules the next one. Safe to call again; a round
  // already running is not restarted.
  void Start();

  // Entries this coordinator is responsible for: enabled by default, delivered
  // by runtime download, and carrying a usable HTTPS url. Static so the
  // selection rule can be tested on its own.
  static std::vector<AdBlockCatalogEntry> SelectSubscribedEntries(
      const std::vector<AdBlockCatalogEntry>& catalog);

  // Shortest update interval among `entries`, which is how often a round has to
  // run for every entry to meet its own freshness requirement.
  static base::TimeDelta RefreshIntervalFor(
      const std::vector<AdBlockCatalogEntry>& entries);

  bool round_is_running_for_testing() const { return round_running_; }
  int completed_rounds_for_testing() const { return completed_rounds_; }
  const std::string& last_error_for_testing() const { return last_error_; }

 private:
  void RunRound();
  void FetchNext();
  void OnFetched(bool success, std::string rules, std::string error);
  void FinishRound(bool success, std::string error);
  void ScheduleNextRound();

  const Fetcher fetcher_;
  const InstallCallback install_;

  std::vector<AdBlockCatalogEntry> entries_;
  size_t next_index_ = 0;
  std::vector<std::string> collected_;
  bool round_running_ = false;
  int completed_rounds_ = 0;
  std::string last_error_;
  base::OneShotTimer refresh_timer_;
  base::WeakPtrFactory<AdBlockCatalogueSubscriber> weak_factory_{this};
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_CATALOGUE_SUBSCRIBER_H_
