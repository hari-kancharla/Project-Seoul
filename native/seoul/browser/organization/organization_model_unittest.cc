// Project Seoul native organization engine.
// Unit tests for OrganizationModel (workspaces, membership, essentials,
// archive, observers). Authored for a capable compile host; not run on the
// authoring machine (8 GiB, no GN/build).

#include "seoul/browser/organization/organization_model.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/time/time.h"
#include "seoul/browser/organization/organization_limits.h"
#include "seoul/browser/organization/organization_observer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class RecordingObserver : public OrganizationModelObserver {
 public:
  void OnOrganizationChanged(const OrganizationChange& change) override {
    changes.push_back(change.type);
  }
  std::vector<OrganizationChangeType> changes;
};

class OrganizationModelTest : public testing::Test {
 protected:
  OrganizationModelTest()
      : model_(base::BindLambdaForTesting([this]() { return clock_; })) {}

  void Advance(base::TimeDelta d) { clock_ += d; }

  WorkspaceId InitDefault() {
    EXPECT_TRUE(model_.EnsureDefaultWorkspace().has_value());
    return model_.default_workspace();
  }

  base::Time clock_ = base::Time::UnixEpoch() + base::Days(100);
  OrganizationModel model_;
};

TEST_F(OrganizationModelTest, FirstRunCreatesExactlyOneDefault) {
  EXPECT_EQ(model_.workspace_count(), 0u);
  ASSERT_TRUE(model_.EnsureDefaultWorkspace().has_value());
  EXPECT_EQ(model_.workspace_count(), 1u);
  WorkspaceId def = model_.default_workspace();
  ASSERT_TRUE(def.is_valid());
  const WorkspaceRecord* w = model_.FindWorkspace(def);
  ASSERT_TRUE(w);
  EXPECT_TRUE(w->is_default);
  EXPECT_FALSE(w->archived);
}

TEST_F(OrganizationModelTest, EnsureDefaultIsIdempotent) {
  ASSERT_TRUE(model_.EnsureDefaultWorkspace().has_value());
  WorkspaceId first = model_.default_workspace();
  ASSERT_TRUE(model_.EnsureDefaultWorkspace().has_value());
  ASSERT_TRUE(model_.EnsureDefaultWorkspace().has_value());
  EXPECT_EQ(model_.workspace_count(), 1u);
  EXPECT_TRUE(model_.default_workspace() == first);
}

TEST_F(OrganizationModelTest, CreateRenameReorder) {
  InitDefault();
  auto created = model_.CreateWorkspace("Work");
  ASSERT_TRUE(created.has_value());
  WorkspaceId id = created.value();
  EXPECT_EQ(model_.workspace_count(), 2u);

  EXPECT_TRUE(model_.RenameWorkspace(id, "Research").has_value());
  EXPECT_EQ(model_.FindWorkspace(id)->name, "Research");

  EXPECT_TRUE(model_.ReorderWorkspace(id, 5).has_value());
  EXPECT_EQ(model_.FindWorkspace(id)->order, 5);

  auto bad_name = model_.CreateWorkspace("   ");
  ASSERT_FALSE(bad_name.has_value());
  EXPECT_EQ(bad_name.error(), OrganizationError::kInvalidName);

  EXPECT_EQ(model_.ReorderWorkspace(id, -1).error(),
            OrganizationError::kInvalidOrder);
  EXPECT_EQ(model_.RenameWorkspace(WorkspaceId::GenerateNew(), "x").error(),
            OrganizationError::kWorkspaceNotFound);
}

// Storage isolation survives being set, being snapshotted, and being loaded
// back. This is the test that was missing when containers were wired, and its
// absence cost an afternoon: the browser-level cookie test could only say the
// containers did not work, not which of four layers had dropped the flag.
TEST_F(OrganizationModelTest, WorkspaceIsolationSetsReadsAndRoundTrips) {
  const WorkspaceId id = InitDefault();
  const WorkspaceRecord* record = model_.FindWorkspace(id);
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->isolated) << "a Space is not isolated until asked";

  ASSERT_TRUE(model_.SetWorkspaceIsolated(id, true).has_value());
  record = model_.FindWorkspace(id);
  ASSERT_TRUE(record);
  EXPECT_TRUE(record->isolated) << "the flag must be readable immediately";

  // Through a snapshot and back, which is the path persistence takes.
  const OrganizationSnapshot snap = model_.ToSnapshot();
  auto isolated_in_snapshot = std::ranges::find_if(
      snap.workspaces,
      [&id](const WorkspaceRecord& w) { return w.id == id; });
  ASSERT_NE(isolated_in_snapshot, snap.workspaces.end());
  EXPECT_TRUE(isolated_in_snapshot->isolated)
      << "the flag must survive ToSnapshot, or persistence loses it";

  OrganizationModel reloaded(
      base::BindLambdaForTesting([this]() { return clock_; }));
  ASSERT_TRUE(reloaded.LoadSnapshot(snap).has_value());
  const WorkspaceRecord* after = reloaded.FindWorkspace(id);
  ASSERT_TRUE(after);
  EXPECT_TRUE(after->isolated)
      << "the flag must survive LoadSnapshot, or it is lost on every restart";

  // And it can be turned off again.
  ASSERT_TRUE(model_.SetWorkspaceIsolated(id, false).has_value());
  record = model_.FindWorkspace(id);
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->isolated);
}

TEST_F(OrganizationModelTest, SetAndClearWorkspaceIcon) {
  const WorkspaceId id = InitDefault();
  RecordingObserver observer;
  model_.AddObserver(&observer);

  ASSERT_TRUE(model_.SetWorkspaceIcon(id, "🧭").has_value());
  EXPECT_EQ(model_.FindWorkspace(id)->icon, "🧭");
  ASSERT_EQ(observer.changes.size(), 1u);
  EXPECT_EQ(observer.changes.back(),
            OrganizationChangeType::kWorkspaceIconChanged);

  EXPECT_EQ(model_.SetWorkspaceIcon(id, "🧭").error(),
            OrganizationError::kNoOpRejected);
  EXPECT_EQ(
      model_.SetWorkspaceIcon(id, "https://example.test/icon.svg").error(),
      OrganizationError::kInvalidIcon);
  EXPECT_EQ(model_.SetWorkspaceIcon(id, std::string(kMaxIconRefLength + 1, 'x'))
                .error(),
            OrganizationError::kInvalidIcon);
  EXPECT_EQ(model_.SetWorkspaceIcon(id, "bad\nicon").error(),
            OrganizationError::kInvalidIcon);
  EXPECT_EQ(model_.SetWorkspaceIcon(WorkspaceId(), "🧭").error(),
            OrganizationError::kInvalidId);
  EXPECT_EQ(model_.SetWorkspaceIcon(WorkspaceId::GenerateNew(), "🧭").error(),
            OrganizationError::kWorkspaceNotFound);

  ASSERT_TRUE(model_.SetWorkspaceIcon(id, std::string()).has_value());
  EXPECT_TRUE(model_.FindWorkspace(id)->icon.empty());
  model_.RemoveObserver(&observer);
}

TEST_F(OrganizationModelTest, SnapshotRejectsInvalidIconReferencesAtomically) {
  const WorkspaceId id = InitDefault();
  ASSERT_TRUE(model_.SetWorkspaceIcon(id, "🌱").has_value());
  OrganizationSnapshot snapshot = model_.ToSnapshot();
  ASSERT_EQ(snapshot.workspaces.size(), 1u);
  snapshot.workspaces[0].icon = "data:image/svg+xml,unsafe";

  EXPECT_EQ(model_.LoadSnapshot(snapshot).error(),
            OrganizationError::kCorruptState);
  EXPECT_EQ(model_.FindWorkspace(id)->icon, "🌱");
}

TEST_F(OrganizationModelTest, ArchiveRestoreAndDefaultProtection) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();

  EXPECT_EQ(model_.ArchiveWorkspace(def).error(),
            OrganizationError::kDefaultWorkspaceProtected);
  EXPECT_EQ(model_.DeleteWorkspace(def).error(),
            OrganizationError::kDefaultWorkspaceProtected);

  ASSERT_TRUE(model_.ArchiveWorkspace(work).has_value());
  EXPECT_TRUE(model_.FindWorkspace(work)->archived);
  // An archived workspace cannot be activated.
  EXPECT_EQ(model_.SetActiveWorkspaceForWindow("win-1", work).error(),
            OrganizationError::kArchivedWorkspaceCannotActivate);

  ASSERT_TRUE(model_.RestoreWorkspace(work).has_value());
  EXPECT_FALSE(model_.FindWorkspace(work)->archived);
}

TEST_F(OrganizationModelTest,
       ArchivingActiveWorkspacePicksDeterministicFallback) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(model_.SetActiveWorkspaceForWindow("win-1", work).has_value());
  EXPECT_TRUE(model_.ActiveWorkspaceForWindow("win-1") == work);

  ASSERT_TRUE(model_.ArchiveWorkspace(work).has_value());
  // Fallback prefers the default workspace.
  EXPECT_TRUE(model_.ActiveWorkspaceForWindow("win-1") == def);
}

TEST_F(OrganizationModelTest, DeleteCascadesMembershipsAndReassignsWindows) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-a", TabRole::kTemporary).has_value());
  ASSERT_TRUE(model_.SetActiveWorkspaceForWindow("win-1", work).has_value());

  ASSERT_TRUE(model_.DeleteWorkspace(work).has_value());
  EXPECT_EQ(model_.workspace_count(), 1u);
  EXPECT_EQ(model_.membership_count(), 0u);
  EXPECT_TRUE(model_.ActiveWorkspaceForWindow("win-1") == def);
}

TEST_F(OrganizationModelTest, MembershipAddDuplicateRemoveMove) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();

  auto m = model_.AddTabMembership(def, "tab-a", TabRole::kTemporary);
  ASSERT_TRUE(m.has_value());
  // A tab_key belongs to at most one workspace: duplicate rejected.
  EXPECT_EQ(model_.AddTabMembership(work, "tab-a", TabRole::kTemporary).error(),
            OrganizationError::kDuplicateMembership);

  ASSERT_TRUE(model_.MoveTabToWorkspace(m.value(), work).has_value());
  EXPECT_TRUE(model_.FindMembership(m.value())->workspace_id == work);
  EXPECT_EQ(model_.MoveTabToWorkspace(m.value(), work).error(),
            OrganizationError::kNoOpRejected);

  ASSERT_TRUE(model_.RemoveTabMembership(m.value()).has_value());
  EXPECT_EQ(model_.membership_count(), 0u);
  EXPECT_EQ(model_.RemoveTabMembership(m.value()).error(),
            OrganizationError::kTabMembershipNotFound);
}

TEST_F(OrganizationModelTest,
       SessionRebindPreservesDurableMembershipAndSplitReferences) {
  const WorkspaceId workspace = InitDefault();
  const TabMembershipId restored =
      model_.AddTabMembership(workspace, "old-session-tab", TabRole::kTemporary)
          .value();
  ASSERT_TRUE(model_.PinTab(restored, "https://example.test/root").has_value());
  ASSERT_TRUE(model_.ReorderTabMembership(restored, 7).has_value());
  const TabMembershipId sibling =
      model_.AddTabMembership(workspace, "sibling-tab", TabRole::kRetained)
          .value();
  const SplitGroupId split =
      model_
          .CreateSplitGroup(workspace, {"old-session-tab", "sibling-tab"}, 0.35,
                            "split-token")
          .value();
  const TabMembershipRecord before = *model_.FindMembership(restored);

  // A collision is rejected before either the membership or its split changes.
  EXPECT_EQ(model_.RebindTabMembership(restored, "sibling-tab").error(),
            OrganizationError::kDuplicateMembership);
  EXPECT_EQ(model_.FindMembership(restored)->tab_key, "old-session-tab");
  EXPECT_EQ(model_.FindSplit(split)->pane_tab_keys[0], "old-session-tab");

  ASSERT_TRUE(
      model_.RebindTabMembership(restored, "new-session-tab").has_value());
  EXPECT_EQ(model_.membership_count(), 2u);
  EXPECT_FALSE(model_.FindMembershipIdByTabKey("old-session-tab").is_valid());
  EXPECT_EQ(model_.FindMembershipIdByTabKey("new-session-tab"), restored);
  const TabMembershipRecord* after = model_.FindMembership(restored);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->workspace_id, before.workspace_id);
  EXPECT_EQ(after->role, before.role);
  EXPECT_EQ(after->saved_root_url, before.saved_root_url);
  EXPECT_EQ(after->order, before.order);
  EXPECT_EQ(after->created_at, before.created_at);
  EXPECT_EQ(after->last_active_at, before.last_active_at);
  ASSERT_EQ(model_.FindSplit(split)->pane_tab_keys.size(), 2u);
  EXPECT_EQ(model_.FindSplit(split)->pane_tab_keys[0], "new-session-tab");
  EXPECT_EQ(model_.FindSplit(split)->pane_tab_keys[1], "sibling-tab");
}

TEST_F(OrganizationModelTest, TabRoleTransitions) {
  WorkspaceId def = InitDefault();
  auto m = model_.AddTabMembership(def, "tab-a", TabRole::kTemporary).value();

  ASSERT_TRUE(model_.RetainTab(m).has_value());
  EXPECT_EQ(model_.FindMembership(m)->role, TabRole::kRetained);

  ASSERT_TRUE(model_.PinTab(m, "https://example.test/root").has_value());
  EXPECT_EQ(model_.FindMembership(m)->role, TabRole::kPinned);
  EXPECT_EQ(model_.FindMembership(m)->saved_root_url,
            "https://example.test/root");

  // Unpinning keeps the tab (becomes retained), it does not close it.
  ASSERT_TRUE(model_.UnpinTab(m).has_value());
  EXPECT_EQ(model_.FindMembership(m)->role, TabRole::kRetained);
  EXPECT_TRUE(model_.FindMembership(m)->saved_root_url.empty());

  ASSERT_TRUE(model_.MarkTabTemporary(m).has_value());
  EXPECT_EQ(model_.FindMembership(m)->role, TabRole::kTemporary);
}

TEST_F(OrganizationModelTest, EssentialsAreGlobalAndSingleIdentity) {
  InitDefault();
  auto e = model_.CreateOrUpdateEssential(EssentialId(), "Mail",
                                          "https://mail.test/");
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(model_.essential_count(), 1u);

  // Update in place keeps the same identity (no duplicate live tab created).
  auto e2 = model_.CreateOrUpdateEssential(e.value(), "Mailbox",
                                           "https://mail.test/inbox");
  ASSERT_TRUE(e2.has_value());
  EXPECT_TRUE(e2.value() == e.value());
  EXPECT_EQ(model_.essential_count(), 1u);
  EXPECT_EQ(model_.FindEssential(e.value())->name, "Mailbox");

  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId::GenerateNew(), "x",
                                         "https://x.test/")
                .error(),
            OrganizationError::kEssentialNotFound);
  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId::GenerateNew(), "x",
                                         "https://mail.test/settings")
                .error(),
            OrganizationError::kEssentialNotFound);
  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId(), "Duplicate mailbox",
                                         "https://mail.test/another-path")
                .error(),
            OrganizationError::kDuplicateEssential);

  ASSERT_TRUE(model_.RemoveEssential(e.value()).has_value());
  EXPECT_EQ(model_.essential_count(), 0u);
}

TEST_F(OrganizationModelTest, EssentialsRejectUnsafeOrMalformedDestinations) {
  InitDefault();
  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId(), "Local file",
                                         "file:///tmp/private")
                .error(),
            OrganizationError::kInvalidUrl);
  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId(), "Script",
                                         "javascript:alert(1)")
                .error(),
            OrganizationError::kInvalidUrl);
  EXPECT_EQ(
      model_.CreateOrUpdateEssential(EssentialId(), "Broken", "not a valid URL")
          .error(),
      OrganizationError::kInvalidUrl);
  EXPECT_EQ(model_.essential_count(), 0u);
}

TEST_F(OrganizationModelTest, UserCreationCapsEssentialsAtTwelve) {
  std::vector<EssentialId> ids;
  for (size_t index = 0; index < kMaxUserEssentials; ++index) {
    auto created = model_.CreateOrUpdateEssential(
        EssentialId(), "Essential " + std::to_string(index),
        "https://essential-" + std::to_string(index) + ".test/");
    ASSERT_TRUE(created.has_value()) << "index " << index;
    ids.push_back(created.value());
  }
  EXPECT_EQ(model_.essential_count(), kMaxUserEssentials);
  EXPECT_EQ(model_
                .CreateOrUpdateEssential(EssentialId(), "One too many",
                                         "https://essential-overflow.test/")
                .error(),
            OrganizationError::kLimitExceeded);

  // Editing an existing Essential remains valid while the deck is full.
  auto updated = model_.CreateOrUpdateEssential(
      ids.front(), "Updated Essential", "https://essential-0.test/inbox");
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated.value(), ids.front());
  EXPECT_EQ(model_.essential_count(), kMaxUserEssentials);
}

TEST_F(OrganizationModelTest, LegacySnapshotCanStillLoadOneHundredEssentials) {
  OrganizationSnapshot snapshot;
  snapshot.schema_version = kOrganizationSchemaVersion;
  for (size_t index = 0; index < kMaxEssentials; ++index) {
    EssentialRecord essential;
    essential.id = EssentialId::GenerateNew();
    essential.name = "Legacy Essential " + std::to_string(index);
    essential.root_url =
        "https://legacy-essential-" + std::to_string(index) + ".test/";
    essential.order = static_cast<int>(index);
    essential.created_at = clock_;
    snapshot.essentials.push_back(std::move(essential));
  }

  ASSERT_TRUE(model_.LoadSnapshot(snapshot).has_value());
  EXPECT_EQ(model_.essential_count(), kMaxEssentials);
}

TEST_F(OrganizationModelTest, AutoArchiveProtectionRules) {
  WorkspaceId def = InitDefault();
  auto temp =
      model_.AddTabMembership(def, "tab-temp", TabRole::kTemporary).value();
  auto retained =
      model_.AddTabMembership(def, "tab-keep", TabRole::kRetained).value();
  auto pinned =
      model_.AddTabMembership(def, "tab-pin", TabRole::kPinned).value();

  const base::TimeDelta threshold = base::Hours(12);
  // Not yet inactive: nothing eligible.
  EXPECT_TRUE(model_.EligibleForAutoArchive({}, clock_, threshold).empty());

  Advance(base::Hours(13));
  base::Time now = clock_;

  // Retained and pinned are never eligible; only the unprotected temporary is.
  std::vector<TabMembershipId> eligible =
      model_.EligibleForAutoArchive({}, now, threshold);
  ASSERT_EQ(eligible.size(), 1u);
  EXPECT_TRUE(eligible[0] == temp);

  // A live condition protects the temporary tab.
  std::map<std::string, TabLiveActivity> activity;
  TabLiveActivity playing;
  playing.playing_media = true;
  activity["tab-temp"] = playing;
  EXPECT_TRUE(model_.EligibleForAutoArchive(activity, now, threshold).empty());

  (void)retained;
  (void)pinned;
}

TEST_F(OrganizationModelTest, ArchiveAndRestoreTab) {
  WorkspaceId def = InitDefault();
  auto m = model_.AddTabMembership(def, "tab-a", TabRole::kTemporary).value();

  ASSERT_TRUE(model_.ArchiveTab(m, "https://example.test/article", "Article")
                  .has_value());
  EXPECT_EQ(model_.membership_count(), 0u);  // not live anymore
  EXPECT_EQ(model_.archived_count(), 1u);
  const ArchivedTabRecord* archived = model_.FindArchivedTab(m);
  ASSERT_TRUE(archived);
  EXPECT_EQ(archived->saved_root_url, "https://example.test/article");
  EXPECT_EQ(archived->title, "Article");

  auto restored = model_.RestoreArchivedTab(m, "tab-a-restored");
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(model_.membership_count(), 1u);
  EXPECT_EQ(model_.archived_count(), 0u);
  EXPECT_EQ(model_.FindMembership(restored.value())->role, TabRole::kTemporary);
}

TEST_F(OrganizationModelTest, AdoptsConfirmedLiveTabFromArchive) {
  WorkspaceId def = InitDefault();
  const WorkspaceId other = model_.CreateWorkspace("Other").value();
  const TabMembershipId archived =
      model_.AddTabMembership(other, "tab-old", TabRole::kRetained).value();
  ASSERT_TRUE(model_.ArchiveTab(archived, "https://example.test", "Example")
                  .has_value());
  const TabMembershipId inserted =
      model_.AddTabMembership(def, "tab-new", TabRole::kTemporary).value();

  ASSERT_TRUE(model_.AdoptRestoredTab(archived, inserted).has_value());
  EXPECT_FALSE(model_.FindArchivedTab(archived));
  const TabMembershipRecord* restored = model_.FindMembership(inserted);
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->workspace_id, other);
  EXPECT_EQ(restored->role, TabRole::kRetained);
  EXPECT_EQ(model_.FindMembershipIdByTabKey("tab-new"), inserted);
}

TEST_F(OrganizationModelTest, ObserversOrderedOncePerCommitNoneOnFailure) {
  RecordingObserver obs;
  model_.AddObserver(&obs);

  ASSERT_TRUE(model_.EnsureDefaultWorkspace().has_value());
  ASSERT_TRUE(model_.CreateWorkspace("Work").has_value());
  // Failed mutation emits no notification.
  EXPECT_FALSE(
      model_.RenameWorkspace(WorkspaceId::GenerateNew(), "x").has_value());

  ASSERT_EQ(obs.changes.size(), 2u);
  EXPECT_EQ(obs.changes[0], OrganizationChangeType::kInitialized);
  EXPECT_EQ(obs.changes[1], OrganizationChangeType::kWorkspaceCreated);

  model_.RemoveObserver(&obs);
  ASSERT_TRUE(model_.CreateWorkspace("Another").has_value());
  EXPECT_EQ(obs.changes.size(), 2u);  // no more after removal
}

TEST_F(OrganizationModelTest, SnapshotRoundTripThroughModel) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-a", TabRole::kPinned).has_value());
  ASSERT_TRUE(
      model_
          .CreateOrUpdateEssential(EssentialId(), "Mail", "https://mail.test/")
          .has_value());

  OrganizationSnapshot snap = model_.ToSnapshot();

  OrganizationModel other;
  ASSERT_TRUE(other.LoadSnapshot(snap).has_value());
  EXPECT_EQ(other.workspace_count(), 2u);
  EXPECT_EQ(other.membership_count(), 1u);
  EXPECT_EQ(other.essential_count(), 1u);
  EXPECT_TRUE(other.default_workspace() == def);
  (void)work;
}

// A custom tab name is Seoul organization metadata. The model stores it, an
// empty name is rejected as a rename (that is what ClearTabCustomTitle is
// for), and clearing returns the tab to showing whatever Chromium says the
// page title is.
TEST_F(OrganizationModelTest, CustomTabTitleIsSetClearedAndBounded) {
  const WorkspaceId def = InitDefault();
  const TabMembershipId tab =
      model_.AddTabMembership(def, "tab-a", TabRole::kRetained).value();

  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "");
  ASSERT_TRUE(model_.SetTabCustomTitle(tab, "Quarterly numbers").has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Quarterly numbers");

  // Setting the same name again changes nothing and is refused rather than
  // emitting a second change for no reason.
  EXPECT_FALSE(model_.SetTabCustomTitle(tab, "Quarterly numbers").has_value());

  // An empty rename is not how a name is removed.
  EXPECT_EQ(model_.SetTabCustomTitle(tab, "").error(),
            OrganizationError::kInvalidName);
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Quarterly numbers");

  EXPECT_EQ(model_.SetTabCustomTitle(tab, std::string(kMaxNameLength + 1, 'x'))
                .error(),
            OrganizationError::kInvalidName);

  ASSERT_TRUE(model_.ClearTabCustomTitle(tab).has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "");
  EXPECT_FALSE(model_.ClearTabCustomTitle(tab).has_value());
}

// Renaming a tab is independent of everything else about it: pinning,
// reordering and moving between workspaces all leave the name alone.
TEST_F(OrganizationModelTest, CustomTabTitleSurvivesRoleAndOrderChanges) {
  const WorkspaceId def = InitDefault();
  const WorkspaceId other = model_.CreateWorkspace("Other").value();
  const TabMembershipId tab =
      model_.AddTabMembership(def, "tab-a", TabRole::kTemporary).value();
  ASSERT_TRUE(model_.SetTabCustomTitle(tab, "Named").has_value());

  ASSERT_TRUE(model_.PinTab(tab, "https://example.test/").has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Named");
  ASSERT_TRUE(model_.UnpinTab(tab).has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Named");
  ASSERT_TRUE(model_.ReorderTabMembership(tab, 5).has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Named");
  ASSERT_TRUE(model_.MoveTabToWorkspace(tab, other).has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Named");
  ASSERT_TRUE(model_.RebindTabMembership(tab, "tab-a-restored").has_value());
  EXPECT_EQ(model_.FindMembership(tab)->custom_title, "Named");
}

// Dissolving a folder must never be a way to lose tabs. Seoul owns the
// grouping; Chromium owns the WebContents.
TEST_F(OrganizationModelTest, DissolvingAFolderKeepsEveryTab) {
  const WorkspaceId def = InitDefault();
  const FolderId folder = model_.CreateFolder(def, "Reading").value();
  const TabMembershipId first =
      model_.AddTabMembership(def, "tab-a", TabRole::kRetained).value();
  const TabMembershipId second =
      model_.AddTabMembership(def, "tab-b", TabRole::kPinned).value();
  ASSERT_TRUE(model_.MoveTabToFolder(first, folder).has_value());
  ASSERT_TRUE(model_.MoveTabToFolder(second, folder).has_value());
  ASSERT_EQ(model_.membership_count(), 2u);

  ASSERT_TRUE(model_.DissolveFolder(folder).has_value());

  EXPECT_EQ(model_.folder_count(), 0u);
  EXPECT_EQ(model_.membership_count(), 2u)
      << "dissolving a folder must not close the tabs inside it";
  EXPECT_FALSE(model_.FindMembership(first)->folder_id.is_valid());
  EXPECT_FALSE(model_.FindMembership(second)->folder_id.is_valid());
  EXPECT_EQ(model_.FindMembership(second)->role, TabRole::kPinned)
      << "and must not change what those tabs are";
}

// A folder belongs to exactly one workspace, and a tab cannot straddle that
// boundary in either direction.
TEST_F(OrganizationModelTest, FoldersDoNotCrossWorkspaces) {
  const WorkspaceId def = InitDefault();
  const WorkspaceId other = model_.CreateWorkspace("Other").value();
  const FolderId here = model_.CreateFolder(def, "Here").value();
  const TabMembershipId tab =
      model_.AddTabMembership(other, "tab-a", TabRole::kRetained).value();

  EXPECT_EQ(model_.MoveTabToFolder(tab, here).error(),
            OrganizationError::kCrossWorkspaceFolder);

  // Moving a tab out of the workspace takes it out of that workspace's folder
  // rather than leaving a reference across the boundary.
  const TabMembershipId local =
      model_.AddTabMembership(def, "tab-b", TabRole::kRetained).value();
  ASSERT_TRUE(model_.MoveTabToFolder(local, here).has_value());
  ASSERT_TRUE(model_.MoveTabToWorkspace(local, other).has_value());
  EXPECT_FALSE(model_.FindMembership(local)->folder_id.is_valid());

  // And deleting a workspace takes its folders with it. The default workspace
  // is protected from deletion, so this uses a third one.
  const WorkspaceId disposable = model_.CreateWorkspace("Disposable").value();
  ASSERT_TRUE(model_.CreateFolder(disposable, "Doomed").has_value());
  EXPECT_EQ(model_.folder_count(), 2u);
  ASSERT_TRUE(model_.DeleteWorkspace(disposable).has_value());
  EXPECT_EQ(model_.folder_count(), 1u)
      << "only the deleted workspace's folders go away";
  EXPECT_TRUE(model_.FindFolder(here));
}

TEST_F(OrganizationModelTest, FolderRenameCollapseAndReorder) {
  const WorkspaceId def = InitDefault();
  const FolderId folder = model_.CreateFolder(def, "First").value();
  EXPECT_EQ(model_.FindFolder(folder)->name, "First");
  EXPECT_FALSE(model_.FindFolder(folder)->collapsed);

  ASSERT_TRUE(model_.RenameFolder(folder, "Renamed").has_value());
  EXPECT_EQ(model_.FindFolder(folder)->name, "Renamed");
  EXPECT_FALSE(model_.RenameFolder(folder, "Renamed").has_value());
  EXPECT_EQ(model_.RenameFolder(folder, "").error(),
            OrganizationError::kInvalidName);

  ASSERT_TRUE(model_.SetFolderCollapsed(folder, true).has_value());
  EXPECT_TRUE(model_.FindFolder(folder)->collapsed);
  EXPECT_FALSE(model_.SetFolderCollapsed(folder, true).has_value());

  const FolderId second = model_.CreateFolder(def, "Second").value();
  EXPECT_EQ(model_.FindFolder(second)->order, 1);
  ASSERT_TRUE(model_.ReorderFolder(second, 0).has_value());
  EXPECT_EQ(model_.FindFolder(second)->order, 0);
  EXPECT_EQ(model_.ReorderFolder(second, -1).error(),
            OrganizationError::kInvalidOrder);

  EXPECT_EQ(model_.RenameFolder(FolderId::GenerateNew(), "x").error(),
            OrganizationError::kFolderNotFound);
}

// Folders and custom names are organization state, so they have to come back
// exactly through the snapshot the session restore path uses.
TEST_F(OrganizationModelTest, FoldersAndNamesSurviveASnapshotRoundTrip) {
  const WorkspaceId def = InitDefault();
  const FolderId folder = model_.CreateFolder(def, "Reading").value();
  ASSERT_TRUE(model_.SetFolderCollapsed(folder, true).has_value());
  const TabMembershipId tab =
      model_.AddTabMembership(def, "tab-a", TabRole::kPinned).value();
  ASSERT_TRUE(model_.MoveTabToFolder(tab, folder).has_value());
  ASSERT_TRUE(model_.SetTabCustomTitle(tab, "Named").has_value());

  OrganizationModel restored;
  ASSERT_TRUE(restored.LoadSnapshot(model_.ToSnapshot()).has_value());

  EXPECT_EQ(restored.folder_count(), 1u);
  const FolderRecord* const restored_folder = restored.FindFolder(folder);
  ASSERT_TRUE(restored_folder);
  EXPECT_EQ(restored_folder->name, "Reading");
  EXPECT_TRUE(restored_folder->collapsed);
  const TabMembershipRecord* const restored_tab = restored.FindMembership(tab);
  ASSERT_TRUE(restored_tab);
  EXPECT_EQ(restored_tab->custom_title, "Named");
  EXPECT_TRUE(restored_tab->folder_id == folder);
}

// A snapshot whose tab points at a folder in another workspace is refused
// whole, rather than loaded with the reference quietly dropped.
TEST_F(OrganizationModelTest, LoadRejectsCrossWorkspaceFolderReference) {
  const WorkspaceId def = InitDefault();
  const WorkspaceId other = model_.CreateWorkspace("Other").value();
  const FolderId folder = model_.CreateFolder(def, "Here").value();
  const TabMembershipId tab =
      model_.AddTabMembership(other, "tab-a", TabRole::kRetained).value();

  OrganizationSnapshot snap = model_.ToSnapshot();
  for (TabMembershipRecord& m : snap.memberships) {
    if (m.id == tab) {
      m.folder_id = folder;
    }
  }

  OrganizationModel target;
  EXPECT_EQ(target.LoadSnapshot(snap).error(),
            OrganizationError::kCrossWorkspaceFolder);
  EXPECT_EQ(target.workspace_count(), 0u) << "a refused load changes nothing";

  // A reference to a folder that does not exist at all is equally refused.
  OrganizationSnapshot dangling = model_.ToSnapshot();
  dangling.folders.clear();
  for (TabMembershipRecord& m : dangling.memberships) {
    m.folder_id = folder;
  }
  EXPECT_EQ(target.LoadSnapshot(dangling).error(),
            OrganizationError::kCorruptState);
}

}  // namespace
}  // namespace seoul
