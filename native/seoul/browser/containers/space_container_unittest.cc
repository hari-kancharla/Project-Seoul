// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/containers/space_container.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::containers {
namespace {

TEST(SpaceContainerTest, PartitionNameIsStableAndPrefixed) {
  EXPECT_EQ("space-abc123", PartitionNameForWorkspace("abc123"));
  // Stability matters more than the exact spelling: the name is where the
  // Space's data physically lives, so changing it orphans everything stored.
  EXPECT_EQ(PartitionNameForWorkspace("abc123"),
            PartitionNameForWorkspace("abc123"));
}

TEST(SpaceContainerTest, DistinctSpacesNeverShareAPartition) {
  EXPECT_NE(PartitionNameForWorkspace("work"),
            PartitionNameForWorkspace("personal"));
}

// The important negative: an id that cannot form a safe partition name must
// produce empty, and callers must read empty as "do not isolate". Sanitising
// instead would let two different ids collapse onto one container, which is a
// data leak between Spaces rather than a cosmetic problem.
TEST(SpaceContainerTest, UnsafeIdsAreRefusedRatherThanSanitised) {
  EXPECT_FALSE(IsIsolatableWorkspaceId(""));
  EXPECT_FALSE(IsIsolatableWorkspaceId("../escape"));
  EXPECT_FALSE(IsIsolatableWorkspaceId("has space"));
  EXPECT_FALSE(IsIsolatableWorkspaceId("slash/es"));
  EXPECT_FALSE(IsIsolatableWorkspaceId("dots.are.paths"));
  EXPECT_FALSE(IsIsolatableWorkspaceId(std::string(97, 'a')));

  EXPECT_EQ("", PartitionNameForWorkspace("../escape"));
  EXPECT_EQ("", PartitionNameForWorkspace(""));
}

TEST(SpaceContainerTest, AcceptsTheIdShapesTheModelActuallyProduces) {
  EXPECT_TRUE(IsIsolatableWorkspaceId("default"));
  EXPECT_TRUE(IsIsolatableWorkspaceId("7f3a9c2e-1b4d"));
  EXPECT_TRUE(IsIsolatableWorkspaceId("Work_2"));
  EXPECT_TRUE(IsIsolatableWorkspaceId(std::string(96, 'a')));
}

TEST(SpaceContainerTest, PartitionDomainIsChromiumLegal) {
  // Chromium requires a non-empty, lowercase-alpha partition domain.
  const std::string domain(kPartitionDomain);
  ASSERT_FALSE(domain.empty());
  for (const char c : domain) {
    EXPECT_TRUE(c >= 'a' && c <= 'z') << "domain must be lowercase alpha";
  }
}

}  // namespace
}  // namespace seoul::containers
