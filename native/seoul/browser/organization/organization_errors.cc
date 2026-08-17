// Project Seoul native organization engine.

#include "seoul/browser/organization/organization_errors.h"

namespace seoul {

const char* OrganizationErrorToString(OrganizationError error) {
  switch (error) {
    case OrganizationError::kInvalidId:
      return "invalid_id";
    case OrganizationError::kInvalidName:
      return "invalid_name";
    case OrganizationError::kInvalidIcon:
      return "invalid_icon";
    case OrganizationError::kInvalidUrl:
      return "invalid_url";
    case OrganizationError::kInvalidOrder:
      return "invalid_order";
    case OrganizationError::kWorkspaceNotFound:
      return "workspace_not_found";
    case OrganizationError::kEssentialNotFound:
      return "essential_not_found";
    case OrganizationError::kTabMembershipNotFound:
      return "tab_membership_not_found";
    case OrganizationError::kFolderNotFound:
      return "folder_not_found";
    case OrganizationError::kCrossWorkspaceFolder:
      return "cross_workspace_folder";
    case OrganizationError::kSplitGroupNotFound:
      return "split_group_not_found";
    case OrganizationError::kRoutingRuleNotFound:
      return "routing_rule_not_found";
    case OrganizationError::kDefaultWorkspaceProtected:
      return "default_workspace_protected";
    case OrganizationError::kArchivedWorkspaceCannotActivate:
      return "archived_workspace_cannot_activate";
    case OrganizationError::kDuplicateEssential:
      return "duplicate_essential";
    case OrganizationError::kDuplicateMembership:
      return "duplicate_membership";
    case OrganizationError::kCrossProfileReference:
      return "cross_profile_reference";
    case OrganizationError::kCrossWorkspaceSplit:
      return "cross_workspace_split";
    case OrganizationError::kInvalidSplitArity:
      return "invalid_split_arity";
    case OrganizationError::kInvalidDividerRatio:
      return "invalid_divider_ratio";
    case OrganizationError::kInvalidActivePane:
      return "invalid_active_pane";
    case OrganizationError::kInvalidUpstreamSplitToken:
      return "invalid_upstream_split_token";
    case OrganizationError::kDuplicateSplitToken:
      return "duplicate_split_token";
    case OrganizationError::kInvalidRoutingRule:
      return "invalid_routing_rule";
    case OrganizationError::kUnsupportedSchema:
      return "unsupported_schema";
    case OrganizationError::kLimitExceeded:
      return "limit_exceeded";
    case OrganizationError::kProtectedTemporaryTab:
      return "protected_temporary_tab";
    case OrganizationError::kProtectedPreview:
      return "protected_preview";
    case OrganizationError::kPersistenceFailure:
      return "persistence_failure";
    case OrganizationError::kCorruptState:
      return "corrupt_state";
    case OrganizationError::kResourceInUse:
      return "resource_in_use";
    case OrganizationError::kNoOpRejected:
      return "no_op_rejected";
  }
  return "unknown";
}

}  // namespace seoul
