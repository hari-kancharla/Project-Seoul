// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_SHELL_TYPES_H_
#define SEOUL_BROWSER_SHELL_SHELL_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_types.h"
#include "seoul/browser/projection/projection_types.h"
#include "seoul/browser/projection/workspace_switcher.h"

namespace seoul {

using ShellWindowKey = LiveWindowKey;

enum class ShellMode {
  kExpanded,
  kCollapsed,
};

// Zen's durable Appearance layout choices. Compact mode is a separate,
// transient presentation and must not be encoded as an Appearance mode.
enum class ShellAppearanceLayoutMode {
  kSingle,
  kMultiple,
  kCollapsed,
};

enum class ShellSection {
  kContextHeader,
  kEssentialsDeck,
  kWorkspacePinned,
  kRetainedTabs,
  kTemporaryTabs,
  kProjectedTabs,
  kUtilityBar,
  kStatus,
  kEmptyWorkspace,
};

enum class ShellItemKind {
  kWorkspace,
  kEssential,
  kPinnedMembership,
  kSectionLabel,
  kUtilityAction,
  kStatusBanner,
};

enum class ShellItemState {
  kNormal,
  kActive,
  kLoading,
  kUnavailable,
  kDisabled,
  kBusy,
};

enum class ShellStatus {
  kCoherent,
  kEmptyWorkspace,
  kSwitchingWorkspace,
  kDegradedProjection,
  kReconciliationRequired,
  kRecoveryRequired,
  kFailOpen,
};

enum class ShellError {
  kInvalidWindow,
  kMutationRejected,
  kCommandRejected,
  kSwitchFailed,
  kConcurrentSwitch,
  kShutdown,
};

enum class ShellUtilityAction {
  kNewTemporaryTab,
  kCommandLauncher,
  kOpenCanvas,
  kOpenBoost,
  kOpenTaskDeck,
  kCreateSplit,
  kToggleCompactMode,
  kSetAppearanceSingle,
  kSetAppearanceMultiple,
  kSetAppearanceCollapsed,
  kOpenSettings,
  kOpenDownloads,
  kReconcile,
  kAcknowledgeRecovery,
};

// Returns the typed layout target for Appearance actions and nullopt for every
// other utility action. Launcher identifiers are never interpreted as modes.
std::optional<ShellAppearanceLayoutMode> AppearanceLayoutModeForAction(
    ShellUtilityAction action);

struct ShellEssentialItem {
  ShellEssentialItem();
  ShellEssentialItem(const ShellEssentialItem&);
  ShellEssentialItem(ShellEssentialItem&&);
  ShellEssentialItem& operator=(const ShellEssentialItem&);
  ShellEssentialItem& operator=(ShellEssentialItem&&);
  ~ShellEssentialItem();
  EssentialId id;
  std::string name;
  std::string icon;
  std::string root_url;
  ShellItemState state = ShellItemState::kNormal;
  bool has_live_tab = false;
  bool live_in_current_window = false;
  bool is_active = false;
  LiveTabKey live_tab;
  LiveWindowKey live_window;

  friend bool operator==(const ShellEssentialItem&,
                         const ShellEssentialItem&) = default;
};

struct ShellPinnedItem {
  TabMembershipId membership_id;
  LiveTabKey tab;
  std::string saved_root_url;
  ShellItemState state = ShellItemState::kNormal;
  bool is_active = false;

  friend bool operator==(const ShellPinnedItem&,
                         const ShellPinnedItem&) = default;
};

struct ShellWorkspaceHeader {
  ShellWorkspaceHeader();
  ShellWorkspaceHeader(const ShellWorkspaceHeader&);
  ShellWorkspaceHeader(ShellWorkspaceHeader&&);
  ShellWorkspaceHeader& operator=(const ShellWorkspaceHeader&);
  ShellWorkspaceHeader& operator=(ShellWorkspaceHeader&&);
  ~ShellWorkspaceHeader();
  WorkspaceId workspace_id;
  std::string name;
  std::string icon;
  bool archived = false;
  bool switching = false;
  ShellItemState state = ShellItemState::kNormal;

  friend bool operator==(const ShellWorkspaceHeader&,
                         const ShellWorkspaceHeader&) = default;
};

// A Zen-style Space switcher item. Workspaces remain the durable model name,
// while the shell presents them as Spaces.
struct ShellSpaceItem {
  ShellSpaceItem();
  ShellSpaceItem(const ShellSpaceItem&);
  ShellSpaceItem(ShellSpaceItem&&);
  ShellSpaceItem& operator=(const ShellSpaceItem&);
  ShellSpaceItem& operator=(ShellSpaceItem&&);
  ~ShellSpaceItem();
  WorkspaceId workspace_id;
  std::string name;
  std::string icon;
  bool is_active = false;
  bool switching = false;

  friend bool operator==(const ShellSpaceItem&,
                         const ShellSpaceItem&) = default;
};

struct ShellSectionInfo {
  ShellSection section = ShellSection::kProjectedTabs;
  std::string label;
  bool visible = false;
  int projected_count = 0;

  friend bool operator==(const ShellSectionInfo&,
                         const ShellSectionInfo&) = default;
};

struct ShellActionEnablement {
  ShellUtilityAction action = ShellUtilityAction::kNewTemporaryTab;
  bool enabled = false;
  std::string disabled_reason;

  friend bool operator==(const ShellActionEnablement&,
                         const ShellActionEnablement&) = default;
};

struct ShellSplitCandidate {
  LiveTabKey tab;
  std::string title;
  std::string origin;

  friend bool operator==(const ShellSplitCandidate&,
                         const ShellSplitCandidate&) = default;
};

struct ShellTaskSummary {
  int total = 0;
  int active = 0;
  int waiting_for_user = 0;
  int paused = 0;
  int failed = 0;

  bool has_attention() const { return waiting_for_user > 0 || paused > 0; }

  friend bool operator==(const ShellTaskSummary&,
                         const ShellTaskSummary&) = default;
};

struct ShellCompactModeState {
  bool available = false;
  bool enabled = false;
  std::string disabled_reason;

  friend bool operator==(const ShellCompactModeState&,
                         const ShellCompactModeState&) = default;
};

struct ShellAppearanceLayoutState {
  bool available = false;
  ShellAppearanceLayoutMode mode = ShellAppearanceLayoutMode::kSingle;
  std::string disabled_reason;

  friend bool operator==(const ShellAppearanceLayoutState&,
                         const ShellAppearanceLayoutState&) = default;
};

struct ShellChatItem {
  std::string id;
  std::string name;
  bool archived = false;
  size_t item_count = 0;

  friend bool operator==(const ShellChatItem&, const ShellChatItem&) = default;
};

struct ShellProjectResources {
  ShellProjectResources();
  ShellProjectResources(const ShellProjectResources&);
  ShellProjectResources(ShellProjectResources&&);
  ShellProjectResources& operator=(const ShellProjectResources&);
  ShellProjectResources& operator=(ShellProjectResources&&);
  ~ShellProjectResources();

  std::vector<ShellChatItem> chats;
  size_t file_count = 0;
  size_t board_count = 0;

  friend bool operator==(const ShellProjectResources&,
                         const ShellProjectResources&) = default;
};

struct ShellSnapshot {
  ShellSnapshot();
  ShellSnapshot(const ShellSnapshot&);
  ShellSnapshot(ShellSnapshot&&);
  ShellSnapshot& operator=(const ShellSnapshot&);
  ShellSnapshot& operator=(ShellSnapshot&&);
  ~ShellSnapshot();
  ShellWindowKey window;
  ShellMode mode = ShellMode::kExpanded;
  ShellStatus status = ShellStatus::kCoherent;
  uint64_t revision = 0;
  ShellWorkspaceHeader workspace;
  std::vector<ShellSpaceItem> spaces;
  std::vector<ShellEssentialItem> essentials;
  std::vector<ShellPinnedItem> pinned_items;
  std::vector<ShellSectionInfo> sections;
  std::vector<ShellActionEnablement> actions;
  ShellTaskSummary tasks;
  ShellProjectResources project_resources;
  ShellCompactModeState compact_mode;
  ShellAppearanceLayoutState appearance_layout;
  bool show_empty_workspace = false;
  bool show_status_banner = false;
  std::string status_message;
  WorkspaceSwitchPhase switch_phase = WorkspaceSwitchPhase::kIdle;
};

struct ShellChange {
  ShellWindowKey window;
  uint64_t revision = 0;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_SHELL_TYPES_H_
