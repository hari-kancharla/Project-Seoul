// Project Seoul bounded per-frame blocker statistics.

#include "seoul/browser/adblock/ad_block_stats_service.h"

namespace seoul::adblock {

AdBlockStatsService::AdBlockStatsService() = default;
AdBlockStatsService::~AdBlockStatsService() = default;

void AdBlockStatsService::RecordBlocked(
    const std::optional<content::GlobalRenderFrameHostToken>& frame_token) {
  ++total_blocked_count_;
  if (!frame_token) {
    return;
  }
  if (!blocked_by_frame_.contains(*frame_token) &&
      blocked_by_frame_.size() >= kMaxTrackedFrames) {
    blocked_by_frame_.erase(blocked_by_frame_.begin());
  }
  ++blocked_by_frame_[*frame_token];
}

uint64_t AdBlockStatsService::GetBlockedCount(
    const content::GlobalRenderFrameHostToken& frame_token) const {
  const auto it = blocked_by_frame_.find(frame_token);
  return it == blocked_by_frame_.end() ? 0u : it->second;
}

}  // namespace seoul::adblock
