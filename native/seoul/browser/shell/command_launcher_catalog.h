// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_COMMAND_LAUNCHER_CATALOG_H_
#define SEOUL_BROWSER_SHELL_COMMAND_LAUNCHER_CATALOG_H_

#include <string>
#include <string_view>
#include <vector>

#include "seoul/browser/lifecycle/live_window_snapshot_types.h"
#include "seoul/browser/organization/organization_types.h"
#include "seoul/browser/shell/shell_types.h"

namespace seoul {

enum class CommandLauncherEntryKind {
  kUtility,
  kWorkspace,
  kEssential,
  kTab,
};

struct CommandLauncherEntry {
  CommandLauncherEntry();
  CommandLauncherEntry(const CommandLauncherEntry &);
  CommandLauncherEntry(CommandLauncherEntry &&);
  CommandLauncherEntry &operator=(const CommandLauncherEntry &);
  CommandLauncherEntry &operator=(CommandLauncherEntry &&);
  ~CommandLauncherEntry();
  std::string id;
  std::string label;
  std::vector<std::string> tokens;
  CommandLauncherEntryKind kind = CommandLauncherEntryKind::kUtility;
  ShellUtilityAction action = ShellUtilityAction::kCommandLauncher;
  WorkspaceId workspace_id;
  EssentialId essential_id;
  LiveWindowKey live_window;
  LiveTabKey live_tab;
  bool enabled = true;
  std::string disabled_reason;
};

class CommandLauncherCatalog {
public:
  // Keep the bubble responsive even when a pathological profile publishes
  // thousands of live tabs. Truncation is deterministic and utilities,
  // workspaces, and Essentials are admitted before tabs.
  static constexpr size_t kMaxCatalogEntries = 5000;
  static constexpr size_t kMaxVisibleResults = 12;

  static std::vector<CommandLauncherEntry>
  BuildEntries(const ShellSnapshot &shell,
               const OrganizationSnapshot &organization,
               const std::vector<LiveWindowSnapshot> &live_windows);
  static std::vector<CommandLauncherEntry>
  Filter(const std::vector<CommandLauncherEntry> &entries,
         std::string_view query, size_t max_results = kMaxVisibleResults);
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_COMMAND_LAUNCHER_CATALOG_H_
