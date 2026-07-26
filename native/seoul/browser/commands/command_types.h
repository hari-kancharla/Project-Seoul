// Project Seoul outbound browser command layer.

#ifndef SEOUL_BROWSER_COMMANDS_COMMAND_TYPES_H_
#define SEOUL_BROWSER_COMMANDS_COMMAND_TYPES_H_

#include <optional>
#include <string>
#include <vector>

#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/organization_ids.h"

namespace seoul {

enum class CommandKind {
  // Model-only.
  kCreateWorkspace,
  kRenameWorkspace,
  kReorderWorkspace,
  kArchiveWorkspace,
  kRestoreWorkspace,
  kDeleteWorkspace,
  kSetActiveWorkspace,
  kMoveTabMembership,
  kMarkTemporary,
  kRetainTab,
  kSetWorkspacePinned,
  kUnsetWorkspacePinned,
  kCreateOrUpdateEssential,
  kRemoveEssential,
  kAddRoutingRule,
  kRemoveRoutingRule,

  // Chromium-affecting.
  kOpenNewTab,  // open Chromium's normal New Tab Page (no URL navigation)
  kOpenTemporaryTab,
  kOpenRetainedTab,
  kNavigateTab,
  kOpenSplitTab,
  kOpenExternal,
  kActivateTab,
  kCloseTab,
  kArchiveTab,
  kRestoreArchivedTab,
  kPinTab,
  kUnpinTab,
  kMoveTabWithinWindow,
  kCreateSplit,
  kDissolveSplit,
};

enum class CommandOrigin {
  kUser,
  kSystem,
  kTest,
};

enum class CommandStatus {
  kCreated,
  kValidating,
  kDispatched,
  kAwaitingObservation,
  kApplied,
  kAppliedWithOrganizationRepairRequired,
  kRejected,
  kCancelled,
  kOutcomeUnknown,
};

enum class CommandForegroundDisposition {
  kForeground,
  kBackground,
};

struct CommandTarget {
  LiveWindowKey window;
  LiveTabKey tab;
  LiveTabKey split_pane_b;
  int destination_index = -1;
  double split_ratio = 0.5;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_COMMANDS_COMMAND_TYPES_H_
