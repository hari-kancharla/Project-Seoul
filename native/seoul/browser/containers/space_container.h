// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_CONTAINERS_SPACE_CONTAINER_H_
#define SEOUL_BROWSER_CONTAINERS_SPACE_CONTAINER_H_

#include <string>
#include <string_view>

namespace seoul::containers {

// Storage isolation, scoped to a Space.
//
// Brave and Firefox both ship this as a concept of its own - Containers,
// Multi-Account Containers - which the user has to learn and then maintain
// alongside whatever they already use to separate their work. Seoul has Spaces
// already, and a Space is exactly the thing a person means when they say "my
// work browsing". So isolation is a property of a Space rather than a second
// idea sitting next to it.
//
// An isolated Space puts its tabs in their own StoragePartition. Cookies,
// localStorage, IndexedDB, cache and service workers all live there and are
// invisible to every other Space, so the same site can be signed into
// differently in two Spaces at once and neither can observe the other.
//
// This header is the naming and validation rule on its own, with no Chromium
// dependencies, because the rule has to be identical everywhere it is applied:
// a partition name computed one way at tab creation and another way at deletion
// leaks data between containers or strands it forever. There is one function
// that produces it and everything calls that.

// The domain every Seoul container lives in. Chromium requires a non-empty
// lowercase-alpha domain; partitions are then unique within it by name.
inline constexpr char kPartitionDomain[] = "seoulspace";

// True when `workspace_id` can be used to form a partition name at all. Ids
// come from the organization model and are opaque, so this refuses anything
// that would produce an ambiguous or empty partition rather than silently
// falling back to the default one - which would be the whole feature quietly
// not working.
bool IsIsolatableWorkspaceId(std::string_view workspace_id);

// The partition name for a Space. Stable for the life of the Space, because it
// is where that Space's data physically lives: changing it would orphan
// everything the user had stored.
//
// Returns empty for an id that fails IsIsolatableWorkspaceId, and callers must
// treat empty as "do not isolate" rather than as "use the default partition".
std::string PartitionNameForWorkspace(std::string_view workspace_id);

}  // namespace seoul::containers

#endif  // SEOUL_BROWSER_CONTAINERS_SPACE_CONTAINER_H_
