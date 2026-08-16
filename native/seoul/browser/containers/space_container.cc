// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/containers/space_container.h"

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace seoul::containers {

namespace {

// Chromium keys a partition directory off this name, so it has to survive a
// round trip through a filesystem path on every supported platform. Ids from
// the organization model are already opaque tokens; this refuses anything that
// is not, rather than sanitising it - two ids that sanitise to the same name
// would share a container, which is a data leak between Spaces.
bool IsSafeIdCharacter(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_';
}

// Long enough for any id the model produces, short enough that the partition
// path cannot approach a platform limit once Chromium adds its own prefixes.
constexpr size_t kMaxWorkspaceIdLength = 96;

}  // namespace

bool IsIsolatableWorkspaceId(std::string_view workspace_id) {
  if (workspace_id.empty() || workspace_id.size() > kMaxWorkspaceIdLength) {
    return false;
  }
  for (const char c : workspace_id) {
    if (!IsSafeIdCharacter(c)) {
      return false;
    }
  }
  return true;
}

std::string PartitionNameForWorkspace(std::string_view workspace_id) {
  if (!IsIsolatableWorkspaceId(workspace_id)) {
    return std::string();
  }
  // Prefixed so a partition belonging to a Space is identifiable on disk and
  // cannot collide with any other partition Seoul or Chromium may introduce.
  return base::StrCat({"space-", workspace_id});
}

}  // namespace seoul::containers
