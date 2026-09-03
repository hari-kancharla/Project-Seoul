// Project Seoul bounded per-frame blocker statistics.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_STATS_SERVICE_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_STATS_SERVICE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "content/public/browser/global_routing_id.h"

namespace seoul::adblock {

// A page's fingerprint receipt: how many times scripts on it read back a
// farbled answer, by surface. Every count is a probe that received scrambled
// data rather than the machine's own.
struct FarbledReadCounts {
  uint64_t canvas = 0;
  uint64_t webgl = 0;
  uint64_t hardware = 0;

  uint64_t total() const { return canvas + webgl + hardware; }
};

class AdBlockStatsService {
 public:
  AdBlockStatsService();
  ~AdBlockStatsService();

  AdBlockStatsService(const AdBlockStatsService&) = delete;
  AdBlockStatsService& operator=(const AdBlockStatsService&) = delete;

  void RecordBlocked(
      const std::optional<content::GlobalRenderFrameHostToken>& frame_token);
  uint64_t GetBlockedCount(
      const content::GlobalRenderFrameHostToken& frame_token) const;
  uint64_t total_blocked_count() const { return total_blocked_count_; }
  size_t tracked_frame_count_for_testing() const {
    return blocked_by_frame_.size();
  }

 private:
  static constexpr size_t kMaxTrackedFrames = 512;

  uint64_t total_blocked_count_ = 0;
  std::map<content::GlobalRenderFrameHostToken, uint64_t> blocked_by_frame_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_STATS_SERVICE_H_
