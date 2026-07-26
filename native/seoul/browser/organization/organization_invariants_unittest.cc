// Project Seoul native organization engine.
// Regression tests for model invariants repaired in native-core V1.
// Authored for later compilation; not run on the authoring machine.

#include <string>

#include "base/test/bind.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/organization_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class OrganizationInvariantsTest : public testing::Test {
 protected:
  OrganizationInvariantsTest()
      : model_(base::BindLambdaForTesting([]() { return base::Time(); })) {}

  WorkspaceId InitDefault() {
    EXPECT_TRUE(model_.EnsureDefaultWorkspace().has_value());
    return model_.default_workspace();
  }

  OrganizationModel model_;
};

TEST_F(OrganizationInvariantsTest, DeleteRoutingSourceWorkspace) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  RoutingRule rule;
  rule.predicate.source_workspace = work;
  rule.predicate.match_type = RoutingMatchType::kAnything;
  rule.result.disposition = RoutingDisposition::kCurrentTab;
  ASSERT_TRUE(model_.AddRoutingRule(rule).has_value());
  ASSERT_TRUE(model_.DeleteWorkspace(work).has_value());
  EXPECT_EQ(model_.routing_rule_count(), 0u);
}

TEST_F(OrganizationInvariantsTest, DeleteRoutingTargetWorkspace) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  RoutingRule rule;
  rule.predicate.match_type = RoutingMatchType::kAnything;
  rule.result.disposition = RoutingDisposition::kSpecificWorkspace;
  rule.result.target_workspace = work;
  ASSERT_TRUE(model_.AddRoutingRule(rule).has_value());
  ASSERT_TRUE(model_.DeleteWorkspace(work).has_value());
  EXPECT_EQ(model_.routing_rule_count(), 0u);
}

TEST_F(OrganizationInvariantsTest, SnapshotAfterDeletionReloads) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-a", TabRole::kTemporary).has_value());
  ASSERT_TRUE(model_.DeleteWorkspace(work).has_value());
  OrganizationSnapshot snap = model_.ToSnapshot();
  OrganizationModel other;
  EXPECT_TRUE(other.LoadSnapshot(snap).has_value());
}

TEST_F(OrganizationInvariantsTest, RejectArchivedDestinationMembership) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(model_.ArchiveWorkspace(work).has_value());
  EXPECT_EQ(model_.AddTabMembership(work, "tab-a", TabRole::kTemporary).error(),
            OrganizationError::kArchivedWorkspaceCannotActivate);
}

TEST_F(OrganizationInvariantsTest, RejectArchivedDestinationMove) {
  WorkspaceId def = InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  auto m = model_.AddTabMembership(def, "tab-a", TabRole::kTemporary).value();
  ASSERT_TRUE(model_.ArchiveWorkspace(work).has_value());
  EXPECT_EQ(model_.MoveTabToWorkspace(m, work).error(),
            OrganizationError::kArchivedWorkspaceCannotActivate);
}

TEST_F(OrganizationInvariantsTest, RejectArchivedDestinationSplit) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-a", TabRole::kRetained).has_value());
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-b", TabRole::kRetained).has_value());
  ASSERT_TRUE(model_.ArchiveWorkspace(work).has_value());
  EXPECT_EQ(
      model_.CreateSplitGroup(work, {"tab-a", "tab-b"}, 0.5, "split-1").error(),
      OrganizationError::kArchivedWorkspaceCannotActivate);
}

TEST_F(OrganizationInvariantsTest, ArchiveRestoreAtCapacity) {
  WorkspaceId def = InitDefault();
  auto m = model_.AddTabMembership(def, "tab-a", TabRole::kTemporary).value();
  for (size_t i = 0; i < kMaxMembershipsPerWorkspace - 1; ++i) {
    ASSERT_TRUE(model_
                    .AddTabMembership(def, "tab-fill-" + std::to_string(i),
                                      TabRole::kTemporary)
                    .has_value());
  }
  ASSERT_TRUE(model_.ArchiveTab(m).has_value());
  ASSERT_TRUE(
      model_.AddTabMembership(def, "tab-replacement", TabRole::kTemporary)
          .has_value());
  EXPECT_EQ(model_.RestoreArchivedTab(m, "tab-a-restored").error(),
            OrganizationError::kLimitExceeded);
}

TEST_F(OrganizationInvariantsTest, ArchiveRestoreRoleBehavior) {
  WorkspaceId def = InitDefault();
  auto pinned = model_.AddTabMembership(def, "tab-a", TabRole::kPinned).value();
  ASSERT_TRUE(model_.PinTab(pinned, "https://example.test/root").has_value());
  ASSERT_TRUE(model_.ArchiveTab(pinned).has_value());
  const OrganizationSnapshot snap = model_.ToSnapshot();
  ASSERT_EQ(snap.archived_tabs.size(), 1u);
  EXPECT_EQ(snap.archived_tabs[0].original_role, TabRole::kPinned);

  auto restored = model_.RestoreArchivedTab(pinned, "tab-a-live");
  ASSERT_TRUE(restored.has_value());
  const TabMembershipRecord* live = model_.FindMembership(restored.value());
  ASSERT_TRUE(live);
  EXPECT_EQ(live->role, TabRole::kRetained);
  EXPECT_TRUE(live->saved_root_url.empty());
}

TEST_F(OrganizationInvariantsTest, DuplicateSplitTokenRejected) {
  WorkspaceId def = InitDefault();
  ASSERT_TRUE(
      model_.AddTabMembership(def, "tab-a", TabRole::kRetained).has_value());
  ASSERT_TRUE(
      model_.AddTabMembership(def, "tab-b", TabRole::kRetained).has_value());
  ASSERT_TRUE(model_.CreateSplitGroup(def, {"tab-a", "tab-b"}, 0.5, "token")
                  .has_value());
  EXPECT_EQ(
      model_.CreateSplitGroup(def, {"tab-a", "tab-b"}, 0.5, "token").error(),
      OrganizationError::kDuplicateSplitToken);
}

TEST_F(OrganizationInvariantsTest, EmptySplitTokenRejected) {
  WorkspaceId def = InitDefault();
  ASSERT_TRUE(
      model_.AddTabMembership(def, "tab-a", TabRole::kRetained).has_value());
  ASSERT_TRUE(
      model_.AddTabMembership(def, "tab-b", TabRole::kRetained).has_value());
  EXPECT_EQ(model_.CreateSplitGroup(def, {"tab-a", "tab-b"}, 0.5, "").error(),
            OrganizationError::kInvalidUpstreamSplitToken);
}

TEST_F(OrganizationInvariantsTest, InvalidEssentialEnumRejected) {
  base::DictValue dict;
  dict.Set("schema_version", kOrganizationSchemaVersion);
  base::ListValue essentials;
  base::DictValue entry;
  entry.Set("id", EssentialId::GenerateNew().value());
  entry.Set("name", "Mail");
  entry.Set("root_url", "https://mail.test/");
  entry.Set("kind", 99);
  essentials.Append(std::move(entry));
  dict.Set("essentials", std::move(essentials));
  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kCorruptState);
}

TEST_F(OrganizationInvariantsTest, DuplicateEssentialOriginSnapshotRejected) {
  OrganizationSnapshot snap;
  snap.schema_version = kOrganizationSchemaVersion;
  WorkspaceRecord workspace;
  workspace.id = WorkspaceId::GenerateNew();
  workspace.name = "Default";
  workspace.is_default = true;
  snap.default_workspace_id = workspace.id;
  snap.workspaces.push_back(workspace);
  EssentialRecord first;
  first.id = EssentialId::GenerateNew();
  first.name = "Mail";
  first.root_url = "https://mail.test/inbox";
  snap.essentials.push_back(first);
  EssentialRecord duplicate;
  duplicate.id = EssentialId::GenerateNew();
  duplicate.name = "Mail settings";
  duplicate.root_url = "https://mail.test/settings";
  snap.essentials.push_back(duplicate);
  EXPECT_EQ(model_.LoadSnapshot(snap).error(),
            OrganizationError::kCorruptState);
}

TEST_F(OrganizationInvariantsTest, OversizedMembershipListRejected) {
  base::DictValue dict;
  dict.Set("schema_version", kOrganizationSchemaVersion);
  base::ListValue memberships;
  for (size_t i = 0; i <= kMaxTotalMemberships; ++i) {
    base::DictValue m;
    m.Set("id", TabMembershipId::GenerateNew().value());
    m.Set("workspace_id", WorkspaceId::GenerateNew().value());
    m.Set("tab_key", "tab-" + std::to_string(i));
    m.Set("role", 0);
    memberships.Append(std::move(m));
  }
  dict.Set("memberships", std::move(memberships));
  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kLimitExceeded);
}

TEST_F(OrganizationInvariantsTest, DuplicateWindowStateRejected) {
  OrganizationSnapshot snap;
  snap.schema_version = kOrganizationSchemaVersion;
  WorkspaceRecord w;
  w.id = WorkspaceId::GenerateNew();
  w.name = "Default";
  w.is_default = true;
  snap.default_workspace_id = w.id;
  snap.workspaces.push_back(w);
  snap.window_states.push_back({"win-1", w.id});
  snap.window_states.push_back({"win-1", w.id});
  EXPECT_EQ(model_.LoadSnapshot(snap).error(),
            OrganizationError::kCorruptState);
}

TEST_F(OrganizationInvariantsTest, InvalidArchiveWorkspaceRejected) {
  OrganizationSnapshot snap;
  snap.schema_version = kOrganizationSchemaVersion;
  WorkspaceRecord w;
  w.id = WorkspaceId::GenerateNew();
  w.name = "Default";
  w.is_default = true;
  snap.default_workspace_id = w.id;
  snap.workspaces.push_back(w);
  ArchivedTabRecord archived;
  archived.original_id = TabMembershipId::GenerateNew();
  archived.workspace_id = WorkspaceId::GenerateNew();
  snap.archived_tabs.push_back(archived);
  EXPECT_EQ(model_.LoadSnapshot(snap).error(),
            OrganizationError::kCorruptState);
}

TEST_F(OrganizationInvariantsTest, RoundTripDeterministic) {
  InitDefault();
  WorkspaceId work = model_.CreateWorkspace("Work").value();
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-a", TabRole::kRetained).has_value());
  ASSERT_TRUE(
      model_.AddTabMembership(work, "tab-b", TabRole::kRetained).has_value());
  ASSERT_TRUE(model_.CreateSplitGroup(work, {"tab-a", "tab-b"}, 0.5, "split-1")
                  .has_value());
  const OrganizationSnapshot first = model_.ToSnapshot();
  base::DictValue dict = SerializeSnapshot(first);
  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_TRUE(parsed.has_value());
  OrganizationModel second;
  ASSERT_TRUE(second.LoadSnapshot(parsed.value()).has_value());
  EXPECT_EQ(SerializeSnapshot(second.ToSnapshot()), dict);
}

}  // namespace
}  // namespace seoul
