// Project Seoul origin-scoped agent permissions.

#include "seoul/browser/policy/agent_permission_service.h"

#include <algorithm>
#include <utility>

#include "url/gurl.h"

namespace seoul {

namespace {

bool IsHttpOrigin(const url::Origin& origin) {
  return !origin.opaque() && origin.GetURL().SchemeIsHTTPOrHTTPS();
}

bool ValidServiceScope(const std::string& scope) {
  if (scope.size() > kMaxAgentServiceScopeLength) {
    return false;
  }
  for (char c : scope) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool SameOptionalOrigin(const url::Origin& lhs, const url::Origin& rhs) {
  if (lhs.opaque() || rhs.opaque()) {
    return lhs.opaque() && rhs.opaque();
  }
  return lhs == rhs;
}

std::string ScopeLabel(const AgentPermissionRequest& request) {
  if (IsHttpOrigin(request.source_origin)) {
    return request.source_origin.Serialize();
  }
  if (!request.service_scope.empty()) {
    return request.service_scope;
  }
  return "this browser window";
}

}  // namespace

AgentPermissionRequest::AgentPermissionRequest() = default;
AgentPermissionRequest::AgentPermissionRequest(const AgentPermissionRequest&) =
    default;
AgentPermissionRequest::AgentPermissionRequest(AgentPermissionRequest&&) =
    default;
AgentPermissionRequest& AgentPermissionRequest::operator=(
    const AgentPermissionRequest&) = default;
AgentPermissionRequest& AgentPermissionRequest::operator=(
    AgentPermissionRequest&&) = default;
AgentPermissionRequest::~AgentPermissionRequest() = default;

AgentPermissionService::AgentPermissionService(Clock clock)
    : clock_(std::move(clock)) {}

AgentPermissionService::~AgentPermissionService() = default;

bool AgentPermissionService::IsValidRequest(
    const AgentPermissionRequest& request) const {
  if (!request.capability.is_valid() || !request.window.is_valid() ||
      !ValidServiceScope(request.service_scope)) {
    return false;
  }
  if (!request.destination_origin.opaque() &&
      !IsHttpOrigin(request.destination_origin)) {
    return false;
  }
  const bool page_capability = request.capability.root_namespace() == "page";
  if (page_capability || request.tab.is_valid()) {
    if (!request.tab.is_valid() || request.frame_scope != "main" ||
        !IsHttpOrigin(request.source_origin)) {
      return false;
    }
  } else if (!request.frame_scope.empty() || !request.source_origin.opaque()) {
    return false;
  }
  if (request.approval == ApprovalPolicy::kFirstUsePerScope &&
      !request.tab.is_valid() && request.service_scope.empty() &&
      request.destination_origin.opaque()) {
    // Never turn a first-use policy into an implicit profile-wide grant.
    // Browser-level navigation capabilities may instead use an exact
    // destination origin, combined with the mandatory live-window scope.
    return false;
  }
  return true;
}

bool AgentPermissionService::ScopeMatches(const AgentPermissionRequest& request,
                                          const Grant& grant) const {
  const AgentPermissionRequest& stored = grant.request;
  return request.capability == stored.capability &&
         request.window == stored.window && request.tab == stored.tab &&
         request.frame_scope == stored.frame_scope &&
         SameOptionalOrigin(request.source_origin, stored.source_origin) &&
         SameOptionalOrigin(request.destination_origin,
                            stored.destination_origin) &&
         request.service_scope == stored.service_scope;
}

AgentPermissionDecision AgentPermissionService::Evaluate(
    const AgentPermissionRequest& request) const {
  if (!IsValidRequest(request)) {
    return {AgentPermissionDecisionKind::kDenied,
            AgentPermissionReason::kInvalidScope};
  }
  if (request.approval == ApprovalPolicy::kAlwaysRequired) {
    return {AgentPermissionDecisionKind::kNeedsApproval,
            AgentPermissionReason::kAlwaysRequired};
  }
  if (request.risk == RiskCategory::kIrreversibleMutation ||
      request.risk == RiskCategory::kExternalSideEffect) {
    return {AgentPermissionDecisionKind::kNeedsApproval,
            AgentPermissionReason::kHighRisk};
  }
  if (request.approval == ApprovalPolicy::kNeverRequired) {
    return {AgentPermissionDecisionKind::kAllowed,
            AgentPermissionReason::kPolicyAllows};
  }
  const base::Time now = clock_.Run();
  for (const Grant& grant : grants_) {
    if (grant.expires_at > now && ScopeMatches(request, grant)) {
      return {AgentPermissionDecisionKind::kAllowed,
              AgentPermissionReason::kExactGrant};
    }
  }
  return {AgentPermissionDecisionKind::kNeedsApproval,
          AgentPermissionReason::kFirstUse};
}

bool AgentPermissionService::GrantFirstUse(
    const AgentPermissionRequest& request,
    base::TimeDelta duration) {
  PruneExpired();
  if (!IsValidRequest(request) ||
      request.approval != ApprovalPolicy::kFirstUsePerScope ||
      request.risk == RiskCategory::kIrreversibleMutation ||
      request.risk == RiskCategory::kExternalSideEffect ||
      duration <= base::TimeDelta() || duration > kMaxAgentGrantDuration) {
    return false;
  }
  const base::Time expires_at = clock_.Run() + duration;
  for (Grant& grant : grants_) {
    if (ScopeMatches(request, grant)) {
      grant.expires_at = expires_at;
      return true;
    }
  }
  if (grants_.size() >= kMaxAgentPermissionGrants) {
    return false;
  }
  grants_.push_back({request, expires_at});
  return true;
}

void AgentPermissionService::RevokeTab(const LiveTabKey& tab) {
  grants_.erase(std::remove_if(grants_.begin(), grants_.end(),
                               [&tab](const Grant& grant) {
                                 return grant.request.tab == tab;
                               }),
                grants_.end());
}

void AgentPermissionService::RevokeWindow(const LiveWindowKey& window) {
  grants_.erase(std::remove_if(grants_.begin(), grants_.end(),
                               [&window](const Grant& grant) {
                                 return grant.request.window == window;
                               }),
                grants_.end());
}

void AgentPermissionService::RevokeAll() {
  grants_.clear();
}

void AgentPermissionService::PruneExpired() {
  const base::Time now = clock_.Run();
  grants_.erase(std::remove_if(grants_.begin(), grants_.end(),
                               [now](const Grant& grant) {
                                 return grant.expires_at <= now;
                               }),
                grants_.end());
}

size_t AgentPermissionService::grant_count() const {
  const base::Time now = clock_.Run();
  return static_cast<size_t>(std::count_if(
      grants_.begin(), grants_.end(),
      [now](const Grant& grant) { return grant.expires_at > now; }));
}

std::string AgentPermissionService::Describe(
    const AgentPermissionRequest& request) const {
  const std::string scope = ScopeLabel(request);
  if (request.risk == RiskCategory::kExternalSideEffect) {
    return "Approve an external side effect through " + scope +
           " for this step only?";
  }
  if (request.risk == RiskCategory::kIrreversibleMutation ||
      request.approval == ApprovalPolicy::kAlwaysRequired) {
    return "Approve " + request.capability.value() + " on " + scope +
           " for this step only?";
  }
  if (request.sensitivity == DataSensitivity::kPageContent) {
    return "Allow Seoul to read page content on " + scope +
           " in this tab for 30 minutes?";
  }
  return "Allow " + request.capability.value() + " through " + scope +
         " for 30 minutes?";
}

}  // namespace seoul
