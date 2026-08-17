// Project Seoul browser organization model.

#include "seoul/browser/organization/organization_types.h"

#include <utility>

namespace seoul {

WorkspaceRecord::WorkspaceRecord() = default;
WorkspaceRecord::WorkspaceRecord(const WorkspaceRecord&) = default;
WorkspaceRecord::WorkspaceRecord(WorkspaceRecord&&) = default;
WorkspaceRecord& WorkspaceRecord::operator=(const WorkspaceRecord&) = default;
WorkspaceRecord& WorkspaceRecord::operator=(WorkspaceRecord&&) = default;
WorkspaceRecord::~WorkspaceRecord() = default;

EssentialRecord::EssentialRecord() = default;
EssentialRecord::EssentialRecord(const EssentialRecord&) = default;
EssentialRecord::EssentialRecord(EssentialRecord&&) = default;
EssentialRecord& EssentialRecord::operator=(const EssentialRecord&) = default;
EssentialRecord& EssentialRecord::operator=(EssentialRecord&&) = default;
EssentialRecord::~EssentialRecord() = default;

TabMembershipRecord::TabMembershipRecord() = default;
TabMembershipRecord::TabMembershipRecord(const TabMembershipRecord&) = default;
TabMembershipRecord::TabMembershipRecord(TabMembershipRecord&&) = default;
TabMembershipRecord& TabMembershipRecord::operator=(const TabMembershipRecord&) = default;
TabMembershipRecord& TabMembershipRecord::operator=(TabMembershipRecord&&) = default;
TabMembershipRecord::~TabMembershipRecord() = default;

OrganizationChange::OrganizationChange() = default;
OrganizationChange::OrganizationChange(OrganizationChangeType type,
                                       WorkspaceId workspace_id,
                                       TabMembershipId membership_id,
                                       FolderId folder_id)
    : type(type),
      workspace_id(std::move(workspace_id)),
      membership_id(std::move(membership_id)),
      folder_id(std::move(folder_id)) {}
OrganizationChange::OrganizationChange(const OrganizationChange&) = default;
OrganizationChange::OrganizationChange(OrganizationChange&&) = default;
OrganizationChange& OrganizationChange::operator=(const OrganizationChange&) =
    default;
OrganizationChange& OrganizationChange::operator=(OrganizationChange&&) =
    default;
OrganizationChange::~OrganizationChange() = default;

FolderRecord::FolderRecord() = default;
FolderRecord::FolderRecord(const FolderRecord&) = default;
FolderRecord::FolderRecord(FolderRecord&&) = default;
FolderRecord& FolderRecord::operator=(const FolderRecord&) = default;
FolderRecord& FolderRecord::operator=(FolderRecord&&) = default;
FolderRecord::~FolderRecord() = default;

SplitGroupRecord::SplitGroupRecord() = default;
SplitGroupRecord::SplitGroupRecord(const SplitGroupRecord&) = default;
SplitGroupRecord::SplitGroupRecord(SplitGroupRecord&&) = default;
SplitGroupRecord& SplitGroupRecord::operator=(const SplitGroupRecord&) = default;
SplitGroupRecord& SplitGroupRecord::operator=(SplitGroupRecord&&) = default;
SplitGroupRecord::~SplitGroupRecord() = default;

RoutingRule::RoutingRule() = default;
RoutingRule::RoutingRule(const RoutingRule&) = default;
RoutingRule::RoutingRule(RoutingRule&&) = default;
RoutingRule& RoutingRule::operator=(const RoutingRule&) = default;
RoutingRule& RoutingRule::operator=(RoutingRule&&) = default;
RoutingRule::~RoutingRule() = default;

RoutingRequest::RoutingRequest() = default;
RoutingRequest::RoutingRequest(const RoutingRequest&) = default;
RoutingRequest::RoutingRequest(RoutingRequest&&) = default;
RoutingRequest& RoutingRequest::operator=(const RoutingRequest&) = default;
RoutingRequest& RoutingRequest::operator=(RoutingRequest&&) = default;
RoutingRequest::~RoutingRequest() = default;

ArchivedTabRecord::ArchivedTabRecord() = default;
ArchivedTabRecord::ArchivedTabRecord(const ArchivedTabRecord&) = default;
ArchivedTabRecord::ArchivedTabRecord(ArchivedTabRecord&&) = default;
ArchivedTabRecord& ArchivedTabRecord::operator=(const ArchivedTabRecord&) = default;
ArchivedTabRecord& ArchivedTabRecord::operator=(ArchivedTabRecord&&) = default;
ArchivedTabRecord::~ArchivedTabRecord() = default;

OrganizationSnapshot::OrganizationSnapshot() = default;
OrganizationSnapshot::OrganizationSnapshot(const OrganizationSnapshot&) = default;
OrganizationSnapshot::OrganizationSnapshot(OrganizationSnapshot&&) = default;
OrganizationSnapshot& OrganizationSnapshot::operator=(const OrganizationSnapshot&) = default;
OrganizationSnapshot& OrganizationSnapshot::operator=(OrganizationSnapshot&&) = default;
OrganizationSnapshot::~OrganizationSnapshot() = default;

}  // namespace seoul
