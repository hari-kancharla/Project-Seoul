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

}  // namespace
}  // namespace seoul
