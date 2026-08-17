// Project Seoul native browser shell V0.

#include "seoul/browser/shell/command_launcher_catalog.h"

#include <algorithm>
#include <ranges>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

const CommandLauncherEntry* FindEntry(
    const std::vector<CommandLauncherEntry>& entries,
    std::string_view id) {
  const auto found = std::ranges::find(entries, id, &CommandLauncherEntry::id);
  return found == entries.end() ? nullptr : &*found;
}

TEST(CommandLauncherCatalogTest, DeterministicRanking) {
  ShellSnapshot snapshot;
  auto entries = CommandLauncherCatalog::BuildEntries(
      snapshot, OrganizationSnapshot(), {});
  ASSERT_FALSE(entries.empty());
  auto filtered = CommandLauncherCatalog::Filter(entries, "tab");
  ASSERT_FALSE(filtered.empty());
  EXPECT_EQ(filtered.front().id, "new_tab");
}

TEST(CommandLauncherCatalogTest, UnknownQueryReturnsEmpty) {
  ShellSnapshot snapshot;
  auto entries = CommandLauncherCatalog::BuildEntries(
      snapshot, OrganizationSnapshot(), {});
  auto filtered = CommandLauncherCatalog::Filter(entries, "zzzz-not-a-command");
  EXPECT_TRUE(filtered.empty());
}

TEST(CommandLauncherCatalogTest, EveryEntryIsExecutableAndSearchable) {
  ShellSnapshot snapshot;
  for (ShellUtilityAction action : {
           ShellUtilityAction::kNewTemporaryTab,
           ShellUtilityAction::kCreateSplit,
           ShellUtilityAction::kOpenCanvas,
           ShellUtilityAction::kOpenBoost,
           ShellUtilityAction::kCapture,
           ShellUtilityAction::kOpenTaskDeck,
           ShellUtilityAction::kSetAppearanceSingle,
           ShellUtilityAction::kSetAppearanceMultiple,
           ShellUtilityAction::kSetAppearanceCollapsed,
           ShellUtilityAction::kToggleCompactMode,
           ShellUtilityAction::kReconcile,
       }) {
    ShellActionEnablement enabled;
    enabled.action = action;
    enabled.enabled = true;
    snapshot.actions.push_back(enabled);
  }
  const auto entries = CommandLauncherCatalog::BuildEntries(
      snapshot, OrganizationSnapshot(), {});
  ASSERT_EQ(entries.size(), 11u);
  for (const CommandLauncherEntry& entry : entries) {
    EXPECT_EQ(entry.kind, CommandLauncherEntryKind::kUtility);
    EXPECT_NE(entry.action, ShellUtilityAction::kCommandLauncher);
    const auto found = CommandLauncherCatalog::Filter(entries, entry.label);
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(found.front().id, entry.id);
  }
}

TEST(CommandLauncherCatalogTest,
     IncludesWorkspacesEssentialsAndTabsWithoutFabricatingTargets) {
  ShellSnapshot shell;
  shell.window = LiveWindowKey::FromSessionId(1);

  OrganizationSnapshot organization;
  WorkspaceRecord current;
  current.id = WorkspaceId::GenerateNew();
  current.name = "Personal";
  organization.workspaces.push_back(current);
  shell.workspace.workspace_id = current.id;
  WorkspaceRecord research;
  research.id = WorkspaceId::GenerateNew();
  research.name = "Research";
  organization.workspaces.push_back(research);
  WorkspaceRecord archived;
  archived.id = WorkspaceId::GenerateNew();
  archived.name = "Old work";
  archived.archived = true;
  organization.workspaces.push_back(archived);

  EssentialRecord mail;
  mail.id = EssentialId::GenerateNew();
  mail.name = "Mail";
  mail.root_url = "https://mail.example.test/";
  organization.essentials.push_back(mail);

  LiveWindowSnapshot current_window;
  current_window.window = shell.window;
  current_window.active_tab = LiveTabKey::FromSessionId(10);
  LiveTabDescriptor active;
  active.tab = current_window.active_tab;
  active.title = "Inbox";
  active.origin = "https://mail.example.test";
  current_window.tabs.push_back(active);
  LiveTabDescriptor docs;
  docs.tab = LiveTabKey::FromSessionId(11);
  docs.title = "Design notes";
  docs.origin = "https://docs.example.test";
  current_window.tabs.push_back(docs);

  LiveWindowSnapshot other_window;
  other_window.window = LiveWindowKey::FromSessionId(2);
  LiveTabDescriptor calendar;
  calendar.tab = LiveTabKey::FromSessionId(20);
  calendar.title = "Calendar";
  calendar.origin = "https://calendar.example.test";
  other_window.tabs.push_back(calendar);

  const auto entries = CommandLauncherCatalog::BuildEntries(
      shell, organization, {current_window, other_window});
  EXPECT_EQ(entries.size(), 17u);

  const auto* current_entry =
      FindEntry(entries, "workspace:" + current.id.value());
  ASSERT_TRUE(current_entry);
  EXPECT_EQ(current_entry->kind, CommandLauncherEntryKind::kWorkspace);
  EXPECT_TRUE(current_entry->enabled);
  EXPECT_EQ(current_entry->workspace_id, current.id);

  const auto* research_entry =
      FindEntry(entries, "workspace:" + research.id.value());
  ASSERT_TRUE(research_entry);
  EXPECT_TRUE(research_entry->enabled);
  EXPECT_EQ(research_entry->workspace_id, research.id);
  EXPECT_FALSE(FindEntry(entries, "workspace:" + archived.id.value()));

  const auto* mail_entry = FindEntry(entries, "essential:" + mail.id.value());
  ASSERT_TRUE(mail_entry);
  EXPECT_EQ(mail_entry->kind, CommandLauncherEntryKind::kEssential);
  EXPECT_EQ(mail_entry->essential_id, mail.id);

  const auto* active_entry = FindEntry(entries, "tab:w-1:t-10");
  ASSERT_TRUE(active_entry);
  EXPECT_TRUE(active_entry->enabled);
  EXPECT_EQ(active_entry->label, "Current tab — Inbox");
  EXPECT_EQ(active_entry->live_window, current_window.window);
  EXPECT_EQ(active_entry->live_tab, active.tab);

  const auto* other_entry = FindEntry(entries, "tab:w-2:t-20");
  ASSERT_TRUE(other_entry);
  EXPECT_TRUE(other_entry->enabled);
  EXPECT_EQ(other_entry->kind, CommandLauncherEntryKind::kTab);
}

TEST(CommandLauncherCatalogTest, FuzzySearchFindsRealResources) {
  ShellSnapshot shell;
  OrganizationSnapshot organization;
  LiveWindowSnapshot window;
  window.window = LiveWindowKey::FromSessionId(7);
  LiveTabDescriptor gmail;
  gmail.tab = LiveTabKey::FromSessionId(8);
  gmail.title = "Gmail";
  gmail.origin = "https://mail.google.com";
  window.tabs.push_back(gmail);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, organization, {window});
  const auto result = CommandLauncherCatalog::Filter(entries, "gml");
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result.front().id, "tab:w-7:t-8");
}

TEST(CommandLauncherCatalogTest, NonLatinSearchDoesNotMatchEntireCatalog) {
  ShellSnapshot shell;
  OrganizationSnapshot organization;
  WorkspaceRecord seoul;
  seoul.id = WorkspaceId::GenerateNew();
  seoul.name = "서울";
  organization.workspaces.push_back(seoul);
  WorkspaceRecord work;
  work.id = WorkspaceId::GenerateNew();
  work.name = "Work";
  organization.workspaces.push_back(work);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, organization, {});
  const auto result = CommandLauncherCatalog::Filter(entries, "서울");
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result.front().id, "workspace:" + seoul.id.value());
}

TEST(CommandLauncherCatalogTest, ResultsAreBoundedAndStable) {
  std::vector<CommandLauncherEntry> entries;
  for (int i = 0; i < 30; ++i) {
    CommandLauncherEntry entry;
    entry.id = "candidate-" + std::to_string(i);
    entry.label = "Candidate " + std::to_string(i);
    entries.push_back(std::move(entry));
  }
  const auto first = CommandLauncherCatalog::Filter(entries, "candidate", 7);
  const auto second = CommandLauncherCatalog::Filter(entries, "candidate", 7);
  ASSERT_EQ(first.size(), 7u);
  ASSERT_EQ(second.size(), 7u);
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].id, second[i].id);
  }
}

TEST(CommandLauncherCatalogTest, SkipsIneligibleAndInvalidLiveTargets) {
  ShellSnapshot shell;
  OrganizationSnapshot organization;
  LiveWindowSnapshot ineligible;
  ineligible.window = LiveWindowKey::FromSessionId(1);
  ineligible.eligible = false;
  LiveTabDescriptor hidden;
  hidden.tab = LiveTabKey::FromSessionId(2);
  hidden.title = "Hidden";
  ineligible.tabs.push_back(hidden);

  LiveWindowSnapshot invalid;
  invalid.window = LiveWindowKey::FromSessionId(3);
  LiveTabDescriptor missing_id;
  missing_id.title = "Missing id";
  invalid.tabs.push_back(missing_id);

  const auto entries = CommandLauncherCatalog::BuildEntries(
      shell, organization, {ineligible, invalid});
  EXPECT_EQ(entries.size(), 11u);
}

TEST(CommandLauncherCatalogTest, SkipsNewTabPlaceholderLiveTarget) {
  ShellSnapshot shell;
  shell.window = LiveWindowKey::FromSessionId(1);
  LiveWindowSnapshot window;
  window.window = shell.window;

  LiveTabDescriptor placeholder;
  placeholder.tab = LiveTabKey::FromSessionId(2);
  placeholder.title = "New Tab";
  placeholder.is_new_tab_placeholder = true;
  window.tabs.push_back(placeholder);

  LiveTabDescriptor ordinary_new_tab_page;
  ordinary_new_tab_page.tab = LiveTabKey::FromSessionId(3);
  ordinary_new_tab_page.title = "New Tab";
  window.tabs.push_back(ordinary_new_tab_page);

  const auto entries = CommandLauncherCatalog::BuildEntries(
      shell, OrganizationSnapshot(), {window});
  EXPECT_FALSE(FindEntry(entries, "tab:w-1:t-2"));
  EXPECT_TRUE(FindEntry(entries, "tab:w-1:t-3"));
}

TEST(CommandLauncherCatalogTest,
     AppearanceEntriesCarryTypedTargetsAndMarkCurrentMode) {
  ShellSnapshot shell;
  shell.appearance_layout.available = true;
  shell.appearance_layout.mode = ShellAppearanceLayoutMode::kMultiple;
  for (ShellUtilityAction action :
       {ShellUtilityAction::kSetAppearanceSingle,
        ShellUtilityAction::kSetAppearanceMultiple,
        ShellUtilityAction::kSetAppearanceCollapsed}) {
    ShellActionEnablement enabled;
    enabled.action = action;
    enabled.enabled = true;
    shell.actions.push_back(enabled);
  }

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  struct Expected {
    std::string_view id;
    ShellUtilityAction action;
    ShellAppearanceLayoutMode mode;
    bool current;
  };
  for (const Expected& expected : {
           Expected{"appearance_single",
                    ShellUtilityAction::kSetAppearanceSingle,
                    ShellAppearanceLayoutMode::kSingle, false},
           Expected{"appearance_multiple",
                    ShellUtilityAction::kSetAppearanceMultiple,
                    ShellAppearanceLayoutMode::kMultiple, true},
           Expected{"appearance_collapsed",
                    ShellUtilityAction::kSetAppearanceCollapsed,
                    ShellAppearanceLayoutMode::kCollapsed, false},
       }) {
    const CommandLauncherEntry* entry = FindEntry(entries, expected.id);
    ASSERT_TRUE(entry);
    EXPECT_TRUE(entry->enabled);
    EXPECT_EQ(entry->kind, CommandLauncherEntryKind::kUtility);
    EXPECT_EQ(entry->action, expected.action);
    EXPECT_EQ(AppearanceLayoutModeForAction(entry->action), expected.mode);
    EXPECT_EQ(entry->shortcut == "Current", expected.current);
  }
  EXPECT_FALSE(
      AppearanceLayoutModeForAction(ShellUtilityAction::kToggleCompactMode)
          .has_value());

  const auto matches = CommandLauncherCatalog::Filter(entries, "appearance");
  ASSERT_EQ(matches.size(), 3u);
  EXPECT_EQ(matches[0].id, "appearance_single");
  EXPECT_EQ(matches[1].id, "appearance_multiple");
  EXPECT_EQ(matches[2].id, "appearance_collapsed");
}

TEST(CommandLauncherCatalogTest, CompactEntryReflectsLiveModeAndAvailability) {
  ShellSnapshot shell;
  ShellActionEnablement compact;
  compact.action = ShellUtilityAction::kToggleCompactMode;
  compact.enabled = true;
  shell.actions.push_back(compact);

  auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  const CommandLauncherEntry* entry = FindEntry(entries, "toggle_compact");
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->enabled);
  EXPECT_EQ(entry->label, "Enter Compact Mode");
  EXPECT_EQ(entry->shortcut, "⌘S");

  shell.compact_mode.enabled = true;
  entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  entry = FindEntry(entries, "toggle_compact");
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->label, "Exit Compact Mode");
  const auto matches = CommandLauncherCatalog::Filter(entries, "zen");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches.front().id, "toggle_compact");
}

// Ranking and intent answer different questions. Filter() is deliberately
// generous so a few letters can reach a command; HasCommandIntent() decides
// whether Return runs one instead of searching, and must not be.
TEST(CommandLauncherCatalogTest, CommandIntentSeparatesNamingFromSearching) {
  ShellSnapshot shell;
  ShellActionEnablement new_tab;
  new_tab.action = ShellUtilityAction::kNewTemporaryTab;
  new_tab.enabled = true;
  shell.actions.push_back(new_tab);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  const CommandLauncherEntry* const entry = FindEntry(entries, "new_tab");
  ASSERT_TRUE(entry);
  ASSERT_EQ(entry->label, "Open New Tab");

  // Naming the command: the whole label, a prefix of it, a word inside it,
  // and a synonym token. The empty palette lists everything it can do.
  for (const char* query :
       {"Open New Tab", "open new", "tab", "temporary", ""}) {
    EXPECT_TRUE(CommandLauncherCatalog::HasCommandIntent(*entry, query))
        << "'" << query << "' names this command";
  }

  // Searching. Each of these reaches the entry through Filter()'s
  // subsequence ranking, and none of them is the user naming a command.
  for (const char* query :
       {"best mechanical keyboards", "pnt", "pen ew ab", "example.com"}) {
    EXPECT_FALSE(CommandLauncherCatalog::HasCommandIntent(*entry, query))
        << "'" << query << "' must be searched, not run as a command";
  }

  // The ranking path still admits it - the point of the assertions above is
  // that being reachable by the ranker is not the same as being named, which
  // is exactly why intent has to be a separate question rather than a
  // threshold on the same score.
  const auto ranked =
      CommandLauncherCatalog::Filter(entries, "pnt", /*max_results=*/entries.size());
  EXPECT_TRUE(std::ranges::any_of(ranked, [](const CommandLauncherEntry& e) {
    return e.id == "new_tab";
  })) << "the fuzzy ranker still reaches this entry for a query that does "
         "not name it";
}

// Arc reaches the Boost editor by typing "New Boost" into the command bar
// (resources.arc.net, "Boosts: Customize Any Website"). Seoul labels the row
// for the page it acts on, so the phrase has to be reachable as a token or
// Arc's own gesture would fall through to a web search.
TEST(CommandLauncherCatalogTest, ArcNewBoostPhraseReachesTheBoostCommand) {
  ShellSnapshot shell;
  ShellActionEnablement boost;
  boost.action = ShellUtilityAction::kOpenBoost;
  boost.enabled = true;
  shell.actions.push_back(boost);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  const CommandLauncherEntry* const entry = FindEntry(entries, "open_boost");
  ASSERT_TRUE(entry);

  for (const char* query : {"New Boost", "new boost", "boost"}) {
    EXPECT_TRUE(CommandLauncherCatalog::HasCommandIntent(*entry, query))
        << "'" << query << "' must reach the Boost editor, not a web search";
  }
  const auto matches = CommandLauncherCatalog::Filter(entries, "new boost");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches.front().id, "open_boost");
}

// Arc creates an Easel by typing "New Easel" into the command bar. Seoul's
// authored spatial surface is Boards, on the Canvas, so the phrase has to
// reach it rather than falling through to a web search.
TEST(CommandLauncherCatalogTest, ArcEaselPhraseReachesTheAuthoredSurface) {
  ShellSnapshot shell;
  ShellActionEnablement canvas;
  canvas.action = ShellUtilityAction::kOpenCanvas;
  canvas.enabled = true;
  shell.actions.push_back(canvas);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  const CommandLauncherEntry* const entry = FindEntry(entries, "open_canvas");
  ASSERT_TRUE(entry);

  for (const char* query : {"New Easel", "easel", "board", "boards"}) {
    EXPECT_TRUE(CommandLauncherCatalog::HasCommandIntent(*entry, query))
        << "'" << query << "' must reach the authored surface, not a search";
  }
  const auto matches = CommandLauncherCatalog::Filter(entries, "new easel");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches.front().id, "open_canvas");
}

// Arc takes a Capture by typing "Capture" into the command bar and dragging a
// rectangle over the page. The phrase has to name the command, and the command
// has to carry the action that starts a capture.
TEST(CommandLauncherCatalogTest, ArcCapturePhraseReachesTheCaptureAction) {
  ShellSnapshot shell;
  ShellActionEnablement capture;
  capture.action = ShellUtilityAction::kCapture;
  capture.enabled = true;
  shell.actions.push_back(capture);

  const auto entries =
      CommandLauncherCatalog::BuildEntries(shell, OrganizationSnapshot(), {});
  const CommandLauncherEntry* const entry = FindEntry(entries, "capture");
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->action, ShellUtilityAction::kCapture);
  EXPECT_TRUE(entry->enabled);

  for (const char* query : {"Capture", "capture", "screenshot", "region"}) {
    EXPECT_TRUE(CommandLauncherCatalog::HasCommandIntent(*entry, query))
        << "'" << query << "' must start a capture, not run a web search";
  }
  const auto matches = CommandLauncherCatalog::Filter(entries, "capture");
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches.front().id, "capture");
}

}  // namespace
}  // namespace seoul
