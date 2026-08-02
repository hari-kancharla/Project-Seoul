// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/vertical_presentation_filter.h"

namespace seoul {

VerticalPresentationFilter::VerticalPresentationFilter() = default;

VerticalPresentationFilter::VerticalPresentationFilter(
    const WindowProjection& projection) {
  UpdateProjection(projection);
}

VerticalPresentationFilter::~VerticalPresentationFilter() = default;

void VerticalPresentationFilter::UpdateProjection(
    const WindowProjection& projection) {
  presented_tabs_.clear();
  explicitly_hidden_tabs_.clear();
  presented_splits_.clear();
  presented_groups_.clear();
  explicitly_hidden_tabs_.insert(projection.hidden_tabs.begin(),
                                 projection.hidden_tabs.end());
  if (disabled_ || projection.status == ProjectionStatus::kFailOpen) {
    return;
  }
  for (const ProjectedTab& tab : projection.tabs) {
    presented_tabs_.insert(tab.tab);
  }
  for (const ProjectedSplit& split : projection.splits) {
    presented_splits_.insert(split.upstream_split_token);
  }
}

void VerticalPresentationFilter::SetDisabled(bool disabled) {
  disabled_ = disabled;
}

bool VerticalPresentationFilter::ShouldPresentTab(LiveTabKey tab) const {
  if (IsExplicitlyHiddenTab(tab)) {
    return false;
  }
  if (disabled_) {
    return true;
  }
  return presented_tabs_.count(tab) > 0;
}

bool VerticalPresentationFilter::IsExplicitlyHiddenTab(LiveTabKey tab) const {
  return explicitly_hidden_tabs_.contains(tab);
}

bool VerticalPresentationFilter::ShouldPresentSplit(
    const std::string& upstream_split_token) const {
  if (disabled_ || upstream_split_token.empty()) {
    return true;
  }
  return presented_splits_.count(upstream_split_token) > 0;
}

bool VerticalPresentationFilter::ShouldPresentGroup(
    const std::string& upstream_group_token) const {
  if (disabled_ || upstream_group_token.empty()) {
    return true;
  }
  return presented_groups_.count(upstream_group_token) > 0;
}

}  // namespace seoul
