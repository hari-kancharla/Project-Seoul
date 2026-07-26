// Project Seoul product runtime: capability execution.

#include "seoul/browser/product/capability_executor.h"

#include <utility>

namespace seoul {

namespace {
// A runaway registration is a bug, not a configuration.
constexpr size_t kMaxExecutors = 2048;
} // namespace

CapabilityRequest::CapabilityRequest() = default;
CapabilityRequest::CapabilityRequest(CapabilityRequest &&) = default;
CapabilityRequest &CapabilityRequest::operator=(CapabilityRequest &&) = default;
CapabilityRequest::~CapabilityRequest() = default;

CapabilityOutcome::CapabilityOutcome() = default;
CapabilityOutcome::CapabilityOutcome(CapabilityOutcome &&) = default;
CapabilityOutcome &CapabilityOutcome::operator=(CapabilityOutcome &&) = default;
CapabilityOutcome::~CapabilityOutcome() = default;

CapabilityExecutorRegistry::CapabilityExecutorRegistry() = default;
CapabilityExecutorRegistry::~CapabilityExecutorRegistry() = default;

bool CapabilityExecutorRegistry::Register(
    std::unique_ptr<CapabilityExecutor> executor) {
  if (!executor || !executor->capability_id().is_valid() ||
      executors_.size() >= kMaxExecutors) {
    return false;
  }
  const std::pair<std::string, int> key{executor->capability_id().value(),
                                        executor->version()};
  if (executors_.count(key)) {
    return false;
  }
  executors_[key] = std::move(executor);
  return true;
}

CapabilityExecutor *CapabilityExecutorRegistry::Find(const ToolId &id,
                                                     int version) const {
  auto it = executors_.find({id.value(), version});
  return it != executors_.end() ? it->second.get() : nullptr;
}

void CapabilityExecutorRegistry::Clear() { executors_.clear(); }

std::vector<std::pair<ToolId, int>>
CapabilityExecutorRegistry::RegisteredCapabilities() const {
  std::vector<std::pair<ToolId, int>> out;
  out.reserve(executors_.size());
  for (const auto &[key, executor] : executors_) {
    out.emplace_back(ToolId::FromString(key.first), key.second);
  }
  return out;
}

CapabilityExecutorRegistry::CompletenessReport
CapabilityExecutorRegistry::CheckCompleteness(
    const std::vector<ToolDescriptor> &descriptors) const {
  CompletenessReport report;
  std::map<std::pair<std::string, int>, bool> descriptor_keys;
  for (const ToolDescriptor &descriptor : descriptors) {
    descriptor_keys[{descriptor.id.value(), descriptor.version}] = true;
    if (!executors_.count({descriptor.id.value(), descriptor.version})) {
      report.descriptors_without_executor.push_back(descriptor.id);
    }
  }
  for (const auto &[key, executor] : executors_) {
    if (!descriptor_keys.count(key)) {
      report.executors_without_descriptor.push_back(
          ToolId::FromString(key.first));
    }
  }
  return report;
}

int CapabilityExecutor::version() const { return 1; }

CapabilityExecutorRegistry::CompletenessReport::CompletenessReport() = default;
CapabilityExecutorRegistry::CompletenessReport::CompletenessReport(
    const CompletenessReport &) = default;
CapabilityExecutorRegistry::CompletenessReport::CompletenessReport(
    CompletenessReport &&) = default;
CapabilityExecutorRegistry::CompletenessReport &
CapabilityExecutorRegistry::CompletenessReport::operator=(
    const CompletenessReport &) = default;
CapabilityExecutorRegistry::CompletenessReport &
CapabilityExecutorRegistry::CompletenessReport::operator=(
    CompletenessReport &&) = default;
CapabilityExecutorRegistry::CompletenessReport::~CompletenessReport() = default;

} // namespace seoul
