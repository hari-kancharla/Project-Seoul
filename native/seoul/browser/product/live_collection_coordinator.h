// Project Seoul Live Collections runtime.
//
// Bridges durable provider-neutral Library definitions to registered typed
// read-only capabilities. It never knows business domains or connector names:
// eligibility comes from descriptor risk/approval/schema metadata, and results
// are mapped only through validated semantic roles.

#ifndef SEOUL_BROWSER_PRODUCT_LIVE_COLLECTION_COORDINATOR_H_
#define SEOUL_BROWSER_PRODUCT_LIVE_COLLECTION_COORDINATOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "seoul/browser/library/library_service.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/capability_executor.h"
#include "seoul/browser/tools/tool_registry.h"

namespace seoul {

enum class LiveCollectionRuntimeError {
  kInvalidDefinition,
  kUnknownCollection,
  kLimitExceeded,
  kSourceUnavailable,
  kSourceNotBackgroundSafe,
  kSourceSchemaUnsupported,
  kSourceInputInvalid,
  kRefreshInProgress,
  kWindowUnavailable,
  kProviderFailed,
  kInvalidResult,
  kRefreshTimedOut,
  kCancelled,
  kShuttingDown,
};

const char* LiveCollectionRuntimeErrorToString(
    LiveCollectionRuntimeError error);

using LiveCollectionRuntimeStatus =
    base::expected<void, LiveCollectionRuntimeError>;
using LiveCollectionUpsertResult =
    base::expected<LiveCollectionId, LiveCollectionRuntimeError>;

struct LiveCollectionSource {
  LiveCollectionSource();
  LiveCollectionSource(const LiveCollectionSource&);
  LiveCollectionSource(LiveCollectionSource&&);
  LiveCollectionSource& operator=(const LiveCollectionSource&);
  LiveCollectionSource& operator=(LiveCollectionSource&&);
  ~LiveCollectionSource();

  ToolId capability;
  int version = 1;
  std::string name;
  std::string description;
  std::string provider;
  bool source_required = false;
  std::string source_field;
  std::string source_description;
  SchemaFieldKind source_kind = SchemaFieldKind::kString;

  friend bool operator==(const LiveCollectionSource&,
                         const LiveCollectionSource&) = default;
};

class LiveCollectionCoordinator {
 public:
  using RefreshCallback =
      base::OnceCallback<void(LiveCollectionRuntimeStatus)>;

  LiveCollectionCoordinator(LibraryService* library,
                            ToolRegistry* capabilities,
                            CapabilityExecutorRegistry* executors);
  LiveCollectionCoordinator(const LiveCollectionCoordinator&) = delete;
  LiveCollectionCoordinator& operator=(const LiveCollectionCoordinator&) =
      delete;
  ~LiveCollectionCoordinator();

  std::vector<LiveCollectionSource> EligibleSources(
      const ToolPermissionContext& context) const;

  LiveCollectionUpsertResult Upsert(
      LiveCollectionDefinition definition,
      const ToolPermissionContext& context);
  LiveCollectionRuntimeStatus SetEnabled(
      const LiveCollectionId& id,
      bool enabled,
      const std::string& scope_window,
      const ToolPermissionContext& context);
  LiveCollectionRuntimeStatus Delete(const LiveCollectionId& id);

  // Starts one refresh. The callback runs exactly once, including for an
  // immediate validation error, provider failure, timeout, or cancellation.
  LiveCollectionRuntimeStatus Refresh(
      const LiveCollectionId& id,
      const LiveWindowKey& window,
      const ToolPermissionContext& context,
      RefreshCallback callback);

  void CancelForWindow(const LiveWindowKey& window);
  bool IsRefreshing(const LiveCollectionId& id) const;
  void Shutdown();

 private:
  struct PendingRefresh;

  const LiveCollectionSource* FindEligibleSource(
      const ToolId& capability,
      const ToolPermissionContext& context,
      std::vector<LiveCollectionSource>* storage) const;
  LiveCollectionRuntimeStatus BuildArgs(
      const LiveCollectionSource& source,
      const ToolDescriptor& descriptor,
      const std::string& source_locator,
      base::DictValue* args) const;
  LiveCollectionRuntimeStatus CompleteWithError(
      const LiveCollectionId& id,
      uint64_t generation,
      LiveCollectionRuntimeError error,
      const std::string& detail);
  void OnRefreshOutcome(const LiveCollectionId& id,
                        uint64_t generation,
                        CapabilityOutcome outcome);
  void OnRefreshTimeout(const LiveCollectionId& id, uint64_t generation);
  RefreshCallback CancelPending(const LiveCollectionId& id);

  raw_ptr<LibraryService> library_;
  raw_ptr<ToolRegistry> capabilities_;
  raw_ptr<CapabilityExecutorRegistry> executors_;
  std::map<LiveCollectionId, std::unique_ptr<PendingRefresh>> pending_;
  bool shutting_down_ = false;
  base::WeakPtrFactory<LiveCollectionCoordinator> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_LIVE_COLLECTION_COORDINATOR_H_
