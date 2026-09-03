// Project Seoul bounded per-frame blocker statistics.

#include "seoul/browser/adblock/ad_block_stats_service.h"

#include <limits>

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

void AdBlockStatsService::RecordFarbledReads(
    const content::GlobalRenderFrameHostToken& page,
    const FarbledReadCounts& counts) {
  if (counts.total() == 0) {
    return;
  }
  if (!farbled_reads_by_page_.contains(page) &&
      farbled_reads_by_page_.size() >= kMaxTrackedFrames) {
    farbled_reads_by_page_.erase(farbled_reads_by_page_.begin());
  }
  // Saturating, not wrapping: a receipt that rolled over to a small number
  // would understate what a page did, which is the one direction it must never
  // fail in.
  const auto add = [](uint64_t& total, uint64_t more) {
    total = (total > std::numeric_limits<uint64_t>::max() - more)
                ? std::numeric_limits<uint64_t>::max()
                : total + more;
  };
  FarbledReadCounts& receipt = farbled_reads_by_page_[page];
  add(receipt.canvas, counts.canvas);
  add(receipt.webgl, counts.webgl);
  add(receipt.hardware, counts.hardware);
}

FarbledReadCounts AdBlockStatsService::GetFarbledReads(
    const content::GlobalRenderFrameHostToken& page) const {
  const auto it = farbled_reads_by_page_.find(page);
  return it == farbled_reads_by_page_.end() ? FarbledReadCounts() : it->second;
}

void AdBlockStatsService::ResetFarbledReads(
    const content::GlobalRenderFrameHostToken& page) {
  farbled_reads_by_page_.erase(page);
}

}  // namespace seoul::adblock
