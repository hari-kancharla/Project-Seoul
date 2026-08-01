// Project Seoul native browser shell V0.

#include "seoul/browser/shell/command_launcher_catalog.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace seoul {
namespace {

CommandLauncherEntry MakeEntry(const std::string& id,
                               const std::string& label,
                               std::initializer_list<std::string> tokens,
                               bool enabled,
                               const std::string& disabled_reason) {
  CommandLauncherEntry entry;
  entry.id = id;
  entry.label = label;
  entry.tokens.assign(tokens);
  entry.enabled = enabled;
  entry.disabled_reason = disabled_reason;
  return entry;
}

std::string Normalize(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool pending_space = false;
  for (unsigned char c : value) {
    // Preserve UTF-8 bytes verbatim so non-Latin titles remain searchable.
    // ASCII case folding is sufficient for commands while avoiding a lossy
    // normalization that would turn a query such as "서울" into an empty
    // string and incorrectly match the entire catalog.
    if (c >= 0x80 || std::isalnum(c)) {
      if (pending_space && !normalized.empty()) {
        normalized.push_back(' ');
      }
      normalized.push_back(c >= 0x80 ? static_cast<char>(c)
                                     : static_cast<char>(std::tolower(c)));
      pending_space = false;
    } else {
      pending_space = !normalized.empty();
    }
  }
  return normalized;
}

// Higher is better. Exact, prefix, and word-boundary matches win over a
// compact subsequence; a non-match is negative. This deliberately stays small
// and deterministic instead of depending on locale or a heavyweight index.
int ScoreValue(std::string_view value, std::string_view normalized_query) {
  const std::string normalized_value = Normalize(value);
  if (normalized_value.empty() || normalized_query.empty()) {
    return normalized_query.empty() ? 1 : -1;
  }
  if (normalized_value == normalized_query) {
    return 100000;
  }
  if (normalized_value.starts_with(normalized_query)) {
    return 90000 - static_cast<int>(normalized_value.size());
  }
  const size_t substring = normalized_value.find(normalized_query);
  if (substring != std::string::npos) {
    const bool word_boundary =
        substring == 0 || normalized_value[substring - 1] == ' ';
    return (word_boundary ? 80000 : 70000) - static_cast<int>(substring) -
           static_cast<int>(normalized_value.size() - normalized_query.size());
  }

  size_t query_index = 0;
  int first_match = -1;
  int previous_match = -2;
  int adjacent = 0;
  int word_boundaries = 0;
  for (size_t i = 0;
       i < normalized_value.size() && query_index < normalized_query.size();
       ++i) {
    if (normalized_value[i] != normalized_query[query_index]) {
      continue;
    }
    if (first_match < 0) {
      first_match = static_cast<int>(i);
    }
    if (previous_match + 1 == static_cast<int>(i)) {
      ++adjacent;
    }
    if (i == 0 || normalized_value[i - 1] == ' ') {
      ++word_boundaries;
    }
    previous_match = static_cast<int>(i);
    ++query_index;
  }
  if (query_index != normalized_query.size()) {
    return -1;
  }
  const int span = previous_match - first_match + 1;
  return 50000 + adjacent * 120 + word_boundaries * 80 - span * 4 - first_match;
}

int ScoreEntry(const CommandLauncherEntry& entry,
               std::string_view normalized_query) {
  if (normalized_query.empty()) {
    return 1;
  }
  int score = ScoreValue(entry.label, normalized_query);
  score = std::max(score, ScoreValue(entry.id, normalized_query) - 1000);
  for (const std::string& token : entry.tokens) {
    score = std::max(score, ScoreValue(token, normalized_query) - 500);
  }
  return score;
}

std::string DisplayTitle(const LiveTabDescriptor& tab) {
  if (!tab.title.empty()) {
    return tab.title;
  }
  if (!tab.origin.empty()) {
    return tab.origin;
  }
  return "Untitled tab";
}

}  // namespace

std::vector<CommandLauncherEntry> CommandLauncherCatalog::BuildEntries(
    const ShellSnapshot& shell,
    const OrganizationSnapshot& organization,
    const std::vector<LiveWindowSnapshot>& live_windows) {
  auto action_enabled = [&](ShellUtilityAction action) {
    for (const ShellActionEnablement& entry : shell.actions) {
      if (entry.action == action) {
        return entry;
      }
    }
    return ShellActionEnablement();
  };

  const ShellActionEnablement new_tab =
      action_enabled(ShellUtilityAction::kNewTemporaryTab);
  const ShellActionEnablement split =
      action_enabled(ShellUtilityAction::kCreateSplit);
  const ShellActionEnablement canvas =
      action_enabled(ShellUtilityAction::kOpenCanvas);
  const ShellActionEnablement boost =
      action_enabled(ShellUtilityAction::kOpenBoost);
  const ShellActionEnablement tasks =
      action_enabled(ShellUtilityAction::kOpenTaskDeck);
  const ShellActionEnablement appearance_single =
      action_enabled(ShellUtilityAction::kSetAppearanceSingle);
  const ShellActionEnablement appearance_multiple =
      action_enabled(ShellUtilityAction::kSetAppearanceMultiple);
  const ShellActionEnablement appearance_collapsed =
      action_enabled(ShellUtilityAction::kSetAppearanceCollapsed);
  const ShellActionEnablement compact =
      action_enabled(ShellUtilityAction::kToggleCompactMode);
  const ShellActionEnablement reconcile =
      action_enabled(ShellUtilityAction::kReconcile);

  std::vector<CommandLauncherEntry> entries;
  entries.push_back(MakeEntry("new_tab", "Open New Tab",
                              {"open", "tab", "temporary"}, new_tab.enabled,
                              new_tab.disabled_reason));
  entries.back().action = ShellUtilityAction::kNewTemporaryTab;
  entries.back().shortcut = "⌘T";
  entries.push_back(MakeEntry("create_split", "Create Split", {"split", "pane"},
                              split.enabled, split.disabled_reason));
  entries.back().action = ShellUtilityAction::kCreateSplit;
  entries.push_back(MakeEntry("open_canvas", "Open Seoul Canvas",
                              {"canvas", "assistant", "voice", "tasks"},
                              canvas.enabled, canvas.disabled_reason));
  entries.back().action = ShellUtilityAction::kOpenCanvas;
  entries.push_back(MakeEntry("open_boost", "Boost This Site",
                              {"boost", "customize", "style", "script", "site"},
                              boost.enabled, boost.disabled_reason));
  entries.back().action = ShellUtilityAction::kOpenBoost;
  entries.push_back(MakeEntry("open_task_deck", "Open Task Deck",
                              {"tasks", "progress", "receipts", "automation"},
                              tasks.enabled, tasks.disabled_reason));
  entries.back().action = ShellUtilityAction::kOpenTaskDeck;
  entries.push_back(
      MakeEntry("appearance_single", "Single Toolbar Layout",
                {"appearance", "layout", "single", "toolbar"},
                appearance_single.enabled, appearance_single.disabled_reason));
  entries.back().action = ShellUtilityAction::kSetAppearanceSingle;
  if (shell.appearance_layout.available &&
      shell.appearance_layout.mode == ShellAppearanceLayoutMode::kSingle) {
    entries.back().shortcut = "Current";
  }
  entries.push_back(MakeEntry(
      "appearance_multiple", "Multiple Toolbars Layout",
      {"appearance", "layout", "multiple", "toolbars"},
      appearance_multiple.enabled, appearance_multiple.disabled_reason));
  entries.back().action = ShellUtilityAction::kSetAppearanceMultiple;
  if (shell.appearance_layout.available &&
      shell.appearance_layout.mode == ShellAppearanceLayoutMode::kMultiple) {
    entries.back().shortcut = "Current";
  }
  entries.push_back(MakeEntry(
      "appearance_collapsed", "Collapsed Toolbar Layout",
      {"appearance", "layout", "collapsed", "toolbar"},
      appearance_collapsed.enabled, appearance_collapsed.disabled_reason));
  entries.back().action = ShellUtilityAction::kSetAppearanceCollapsed;
  if (shell.appearance_layout.available &&
      shell.appearance_layout.mode == ShellAppearanceLayoutMode::kCollapsed) {
    entries.back().shortcut = "Current";
  }
  entries.push_back(MakeEntry(
      "toggle_compact",
      shell.compact_mode.enabled ? "Exit Compact Mode" : "Enter Compact Mode",
      {"compact", "focus", "hide", "chrome", "toolbar", "zen"}, compact.enabled,
      compact.disabled_reason));
  entries.back().action = ShellUtilityAction::kToggleCompactMode;
  entries.back().shortcut = "⌘S";
  entries.push_back(MakeEntry("reconcile", "Run Reconciliation",
                              {"reconcile", "recovery", "degraded"},
                              reconcile.enabled, reconcile.disabled_reason));
  entries.back().action = ShellUtilityAction::kReconcile;

  for (const WorkspaceRecord& workspace : organization.workspaces) {
    if (workspace.archived) {
      continue;
    }
    if (entries.size() >= kMaxCatalogEntries) {
      break;
    }
    CommandLauncherEntry entry;
    entry.id = "workspace:" + workspace.id.value();
    entry.label =
        (workspace.id == shell.workspace.workspace_id ? "Current workspace — "
                                                      : "Switch workspace — ") +
        workspace.name;
    entry.tokens = {"workspace", "space", workspace.name, workspace.icon};
    entry.kind = CommandLauncherEntryKind::kWorkspace;
    entry.workspace_id = workspace.id;
    entries.push_back(std::move(entry));
  }

  for (const EssentialRecord& essential : organization.essentials) {
    if (entries.size() >= kMaxCatalogEntries) {
      break;
    }
    CommandLauncherEntry entry;
    entry.id = "essential:" + essential.id.value();
    entry.label = "Open Essential — " + essential.name;
    entry.tokens = {"essential", "favorite", essential.name, essential.icon,
                    essential.root_url};
    entry.kind = CommandLauncherEntryKind::kEssential;
    entry.essential_id = essential.id;
    entry.enabled = !essential.root_url.empty();
    entry.disabled_reason =
        entry.enabled ? "" : "This Essential has no destination.";
    entries.push_back(std::move(entry));
  }

  for (const LiveWindowSnapshot& window : live_windows) {
    if (!window.window.is_valid() || !window.eligible) {
      continue;
    }
    for (const LiveTabDescriptor& tab : window.tabs) {
      if (entries.size() >= kMaxCatalogEntries) {
        break;
      }
      if (!tab.tab.is_valid() || tab.is_new_tab_placeholder) {
        continue;
      }
      CommandLauncherEntry entry;
      entry.id = "tab:" + window.window.value() + ":" + tab.tab.value();
      const bool active =
          window.window == shell.window && tab.tab == window.active_tab;
      entry.label =
          (active ? "Current tab — " : "Activate tab — ") + DisplayTitle(tab);
      entry.tokens = {
          "tab", "page", tab.title, tab.origin,
          window.window == shell.window ? "this window" : "other window"};
      entry.kind = CommandLauncherEntryKind::kTab;
      entry.live_window = window.window;
      entry.live_tab = tab.tab;
      entries.push_back(std::move(entry));
    }
    if (entries.size() >= kMaxCatalogEntries) {
      break;
    }
  }
  return entries;
}

std::vector<CommandLauncherEntry> CommandLauncherCatalog::Filter(
    const std::vector<CommandLauncherEntry>& entries,
    std::string_view query,
    size_t max_results) {
  if (max_results == 0) {
    return {};
  }
  const std::string normalized_query = Normalize(query);
  struct RankedEntry {
    size_t original_index = 0;
    int score = std::numeric_limits<int>::min();
  };
  std::vector<RankedEntry> ranked;
  ranked.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const int score = ScoreEntry(entries[i], normalized_query);
    if (score >= 0) {
      ranked.push_back({i, score});
    }
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const RankedEntry& a, const RankedEntry& b) {
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     return a.original_index < b.original_index;
                   });
  std::vector<CommandLauncherEntry> filtered;
  filtered.reserve(std::min(max_results, ranked.size()));
  for (const RankedEntry& match : ranked) {
    if (filtered.size() >= max_results) {
      break;
    }
    filtered.push_back(entries[match.original_index]);
  }
  return filtered;
}

CommandLauncherEntry::CommandLauncherEntry() = default;
CommandLauncherEntry::CommandLauncherEntry(const CommandLauncherEntry&) =
    default;
CommandLauncherEntry::CommandLauncherEntry(CommandLauncherEntry&&) = default;
CommandLauncherEntry& CommandLauncherEntry::operator=(
    const CommandLauncherEntry&) = default;
CommandLauncherEntry& CommandLauncherEntry::operator=(CommandLauncherEntry&&) =
    default;
CommandLauncherEntry::~CommandLauncherEntry() = default;

}  // namespace seoul
