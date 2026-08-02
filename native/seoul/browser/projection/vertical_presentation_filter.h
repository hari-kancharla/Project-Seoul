// Project Seoul workspace projection engine V0.
// Presentation filter predicate for vertical tab views.

#ifndef SEOUL_BROWSER_PROJECTION_VERTICAL_PRESENTATION_FILTER_H_
#define SEOUL_BROWSER_PROJECTION_VERTICAL_PRESENTATION_FILTER_H_

#include <set>

#include "seoul/browser/projection/projection_types.h"

namespace seoul {

class VerticalPresentationFilter {
 public:
  VerticalPresentationFilter();
  explicit VerticalPresentationFilter(const WindowProjection& projection);
  ~VerticalPresentationFilter();

  void UpdateProjection(const WindowProjection& projection);
  void SetDisabled(bool disabled);
  bool disabled() const { return disabled_; }

  bool ShouldPresentTab(LiveTabKey tab) const;
  bool IsExplicitlyHiddenTab(LiveTabKey tab) const;
  bool ShouldPresentSplit(const std::string& upstream_split_token) const;
  bool ShouldPresentGroup(const std::string& upstream_group_token) const;
  const std::set<LiveTabKey>& presented_tabs() const { return presented_tabs_; }

 private:
  bool disabled_ = false;
  std::set<LiveTabKey> presented_tabs_;
  std::set<LiveTabKey> explicitly_hidden_tabs_;
  std::set<std::string> presented_splits_;
  std::set<std::string> presented_groups_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PROJECTION_VERTICAL_PRESENTATION_FILTER_H_
