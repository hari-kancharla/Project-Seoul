// Project Seoul Shell.

#include "seoul/browser/shell/shell_types.h"

namespace seoul {

ShellEssentialItem::ShellEssentialItem() = default;
ShellEssentialItem::ShellEssentialItem(const ShellEssentialItem&) = default;
ShellEssentialItem::ShellEssentialItem(ShellEssentialItem&&) = default;
ShellEssentialItem& ShellEssentialItem::operator=(const ShellEssentialItem&) = default;
ShellEssentialItem& ShellEssentialItem::operator=(ShellEssentialItem&&) = default;
ShellEssentialItem::~ShellEssentialItem() = default;

ShellWorkspaceHeader::ShellWorkspaceHeader() = default;
ShellWorkspaceHeader::ShellWorkspaceHeader(const ShellWorkspaceHeader&) = default;
ShellWorkspaceHeader::ShellWorkspaceHeader(ShellWorkspaceHeader&&) = default;
ShellWorkspaceHeader& ShellWorkspaceHeader::operator=(const ShellWorkspaceHeader&) = default;
ShellWorkspaceHeader& ShellWorkspaceHeader::operator=(ShellWorkspaceHeader&&) = default;
ShellWorkspaceHeader::~ShellWorkspaceHeader() = default;

ShellProjectResources::ShellProjectResources() = default;
ShellProjectResources::ShellProjectResources(const ShellProjectResources&) =
    default;
ShellProjectResources::ShellProjectResources(ShellProjectResources&&) =
    default;
ShellProjectResources& ShellProjectResources::operator=(
    const ShellProjectResources&) = default;
ShellProjectResources& ShellProjectResources::operator=(
    ShellProjectResources&&) = default;
ShellProjectResources::~ShellProjectResources() = default;

ShellSnapshot::ShellSnapshot() = default;
ShellSnapshot::ShellSnapshot(const ShellSnapshot&) = default;
ShellSnapshot::ShellSnapshot(ShellSnapshot&&) = default;
ShellSnapshot& ShellSnapshot::operator=(const ShellSnapshot&) = default;
ShellSnapshot& ShellSnapshot::operator=(ShellSnapshot&&) = default;
ShellSnapshot::~ShellSnapshot() = default;

}  // namespace seoul
