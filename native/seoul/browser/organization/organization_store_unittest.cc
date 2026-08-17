// Project Seoul native organization engine.
// Unit tests for bounded, versioned persistence. Authored for a capable compile
// host.

#include "seoul/browser/organization/organization_store.h"

#include <algorithm>

#include "base/test/bind.h"
#include "base/values.h"
#include "seoul/browser/organization/organization_limits.h"
#include "seoul/browser/organization/organization_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

OrganizationSnapshot BuildPopulatedSnapshot() {
  OrganizationModel model;
  CHECK(model.EnsureDefaultWorkspace().has_value());
  WorkspaceId work = model.CreateWorkspace("Work").value();
  CHECK(model.SetWorkspaceIcon(work, "zen-icon:briefcase").has_value());
  CHECK(model.AddTabMembership(work, "tab-a", TabRole::kPinned).has_value());
  CHECK(model.AddTabMembership(work, "tab-b", TabRole::kRetained).has_value());
  CHECK(model.CreateSplitGroup(work, {"tab-a", "tab-b"}, 0.5, "token")
            .has_value());
  CHECK(
      model.CreateOrUpdateEssential(EssentialId(), "Mail", "https://mail.test/")
          .has_value());
  return model.ToSnapshot();
}

TEST(OrganizationStoreTest, RoundTrip) {
  OrganizationSnapshot snap = BuildPopulatedSnapshot();
  base::DictValue dict = SerializeSnapshot(snap);

  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_TRUE(parsed.has_value());

  OrganizationModel restored;
  ASSERT_TRUE(restored.LoadSnapshot(parsed.value()).has_value());
  EXPECT_EQ(restored.workspace_count(), 2u);
  EXPECT_EQ(restored.membership_count(), 2u);
  EXPECT_EQ(restored.split_count(), 1u);
  EXPECT_EQ(restored.essential_count(), 1u);
  ASSERT_EQ(parsed->workspaces.size(), 2u);
  const auto work =
      std::ranges::find(parsed->workspaces, "Work", &WorkspaceRecord::name);
  ASSERT_NE(work, parsed->workspaces.end());
  EXPECT_EQ(restored.FindWorkspace(work->id)->icon, "zen-icon:briefcase");
}

TEST(OrganizationStoreTest, DeterministicOutput) {
  OrganizationSnapshot snap = BuildPopulatedSnapshot();
  EXPECT_EQ(SerializeSnapshot(snap), SerializeSnapshot(snap));
}

TEST(OrganizationStoreTest, UnsupportedFutureSchemaRejected) {
  base::DictValue dict = SerializeSnapshot(BuildPopulatedSnapshot());
  dict.Set("schema_version", kOrganizationSchemaVersion + 1);
  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error(), OrganizationError::kUnsupportedSchema);
}

TEST(OrganizationStoreTest, MissingSchemaVersionRejected) {
  base::DictValue dict = SerializeSnapshot(BuildPopulatedSnapshot());
  dict.Remove("schema_version");
  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kCorruptState);
}

TEST(OrganizationStoreTest, MalformedAndMissingFields) {
  // A workspace entry missing the required "id" field is corrupt.
  base::DictValue dict;
  dict.Set("schema_version", kOrganizationSchemaVersion);
  base::ListValue workspaces;
  base::DictValue bad;
  bad.Set("name", "NoId");
  workspaces.Append(std::move(bad));
  dict.Set("workspaces", std::move(workspaces));
  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kCorruptState);

  // A non-dict list entry is corrupt.
  base::DictValue dict2;
  dict2.Set("schema_version", kOrganizationSchemaVersion);
  base::ListValue bad_list;
  bad_list.Append("not-a-dict");
  dict2.Set("workspaces", std::move(bad_list));
  EXPECT_EQ(DeserializeSnapshot(dict2).error(),
            OrganizationError::kCorruptState);
}

TEST(OrganizationStoreTest, UnknownFieldsIgnored) {
  base::DictValue dict = SerializeSnapshot(BuildPopulatedSnapshot());
  dict.Set("seoul_future_unknown_key", "ignored");
  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  EXPECT_TRUE(
      parsed.has_value());  // forward-compatible: extra keys are ignored
}

TEST(OrganizationStoreTest, EmptyDictYieldsEmptyValidSnapshot) {
  base::DictValue dict;
  dict.Set("schema_version", kOrganizationSchemaVersion);
  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed.value().workspaces.empty());
}

TEST(OrganizationStoreTest, InvalidReferenceCaughtByLoad) {
  // Structurally valid, semantically invalid: a membership referencing a
  // missing workspace. The store parses it; LoadSnapshot rejects it.
  base::DictValue dict;
  dict.Set("schema_version", kOrganizationSchemaVersion);
  base::ListValue members;
  base::DictValue m;
  m.Set("id", WorkspaceId::GenerateNew().value());            // any valid uuid
  m.Set("workspace_id", WorkspaceId::GenerateNew().value());  // dangling
  m.Set("tab_key", "tab-a");
  m.Set("role", 0);
  members.Append(std::move(m));
  dict.Set("memberships", std::move(members));

  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_TRUE(parsed.has_value());
  OrganizationModel model;
  EXPECT_EQ(model.LoadSnapshot(parsed.value()).error(),
            OrganizationError::kCorruptState);
}

TEST(OrganizationStoreTest, FailedLoadPreservesLastKnownValid) {
  OrganizationModel model;
  ASSERT_TRUE(model.LoadSnapshot(BuildPopulatedSnapshot()).has_value());
  size_t before = model.workspace_count();

  // A corrupt snapshot (dangling split reference) must not destroy current
  // state.
  OrganizationSnapshot corrupt;
  corrupt.schema_version = kOrganizationSchemaVersion;
  SplitGroupRecord s;
  s.id = SplitGroupId::GenerateNew();
  s.workspace_id = WorkspaceId::GenerateNew();  // dangling
  s.pane_tab_keys = {"x", "y"};
  s.divider_ratio = 0.5;
  corrupt.splits.push_back(s);
  EXPECT_FALSE(model.LoadSnapshot(corrupt).has_value());
  EXPECT_EQ(model.workspace_count(), before);  // unchanged
}

TEST(OrganizationStoreTest, SizeLimitHelper) {
  // A normal snapshot is well within the limit.
  EXPECT_TRUE(
      SerializedSizeWithinLimit(SerializeSnapshot(BuildPopulatedSnapshot())));
}

// Note: incognito / off-the-record non-persistence is enforced at the factory
// layer (SeoulOrganizationServiceFactory restricts the service to eligible
// regular profiles via ProfileSelections), so no organization store exists for
// an off-the-record profile. That boundary is covered by browser-level tests on
// a capable host, not by these pure-model unit tests.

// The migration that matters: a profile written by the previous schema must
// still load. Before folders and custom titles existed the store rejected any
// version but its own, so bumping the version without this path would have
// made every existing profile read as unsupported and lose its organization.
TEST(OrganizationStoreTest, PreFoldersSchemaMigratesForward) {
  OrganizationSnapshot snap = BuildPopulatedSnapshot();
  base::DictValue dict = SerializeSnapshot(snap);

  // Rewrite the document as the previous schema actually wrote it: version 1,
  // no folder list, and no per-membership folder or custom-title fields.
  dict.Set("schema_version", kOrganizationSchemaVersionWithoutFolders);
  dict.Remove("folders");
  base::ListValue* const memberships = dict.FindList("memberships");
  ASSERT_TRUE(memberships);
  ASSERT_FALSE(memberships->empty());
  for (base::Value& value : *memberships) {
    base::DictValue* const membership = value.GetIfDict();
    ASSERT_TRUE(membership);
    membership->Remove("folder_id");
    membership->Remove("custom_title");
  }

  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(dict);
  ASSERT_TRUE(parsed.has_value())
      << "a profile from the previous schema must still load";
  EXPECT_EQ(parsed->schema_version, kOrganizationSchemaVersion)
      << "and is stamped forward so the next save writes the new schema";

  // Everything the old schema carried is still there.
  EXPECT_EQ(parsed->workspaces.size(), snap.workspaces.size());
  EXPECT_EQ(parsed->memberships.size(), snap.memberships.size());
  EXPECT_EQ(parsed->essentials.size(), snap.essentials.size());
  EXPECT_EQ(parsed->splits.size(), snap.splits.size());
  // The new fields read as unset, which is what their absence meant.
  EXPECT_TRUE(parsed->folders.empty());
  for (const TabMembershipRecord& m : parsed->memberships) {
    EXPECT_TRUE(m.custom_title.empty());
    EXPECT_FALSE(m.folder_id.is_valid());
  }

  // And the migrated snapshot is loadable, not merely parseable.
  OrganizationModel restored;
  EXPECT_TRUE(restored.LoadSnapshot(parsed.value()).has_value());
}

// Migrating forward is not the same as believing anything. A document that
// claims the old version while carrying new-schema folders was not written by
// any version of this code.
TEST(OrganizationStoreTest, PreFoldersSchemaCarryingFoldersIsRefused) {
  OrganizationModel model;
  CHECK(model.EnsureDefaultWorkspace().has_value());
  CHECK(model.CreateFolder(model.default_workspace(), "Reading").has_value());
  base::DictValue dict = SerializeSnapshot(model.ToSnapshot());
  dict.Set("schema_version", kOrganizationSchemaVersionWithoutFolders);

  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kCorruptState);
}

TEST(OrganizationStoreTest, UnknownFutureSchemaIsStillRejected) {
  base::DictValue dict = SerializeSnapshot(BuildPopulatedSnapshot());
  dict.Set("schema_version", kOrganizationSchemaVersion + 1);
  EXPECT_EQ(DeserializeSnapshot(dict).error(),
            OrganizationError::kUnsupportedSchema);
}

// Folders and custom names survive the on-disk round trip, not just the
// in-memory one.
TEST(OrganizationStoreTest, FoldersAndCustomTitlesRoundTrip) {
  OrganizationModel model;
  CHECK(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId def = model.default_workspace();
  const FolderId folder = model.CreateFolder(def, "Reading").value();
  CHECK(model.SetFolderCollapsed(folder, true).has_value());
  const TabMembershipId tab =
      model.AddTabMembership(def, "tab-a", TabRole::kPinned).value();
  CHECK(model.MoveTabToFolder(tab, folder).has_value());
  CHECK(model.SetTabCustomTitle(tab, "Quarterly numbers").has_value());

  MutationResult<OrganizationSnapshot> parsed =
      DeserializeSnapshot(SerializeSnapshot(model.ToSnapshot()));
  ASSERT_TRUE(parsed.has_value());

  OrganizationModel restored;
  ASSERT_TRUE(restored.LoadSnapshot(parsed.value()).has_value());
  const FolderRecord* const restored_folder = restored.FindFolder(folder);
  ASSERT_TRUE(restored_folder);
  EXPECT_EQ(restored_folder->name, "Reading");
  EXPECT_TRUE(restored_folder->collapsed);
  const TabMembershipRecord* const restored_tab = restored.FindMembership(tab);
  ASSERT_TRUE(restored_tab);
  EXPECT_EQ(restored_tab->custom_title, "Quarterly numbers");
  EXPECT_TRUE(restored_tab->folder_id == folder);
}

}  // namespace
}  // namespace seoul
