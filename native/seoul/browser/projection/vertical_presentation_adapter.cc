// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/vertical_presentation_adapter.h"

#include <functional>

// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"  // nogncheck
#include "chrome/browser/ui/views/tabs/vertical/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/vertical/tab_collection_node.h"
#include "components/tabs/public/tab_interface.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

namespace seoul {
namespace {

LiveTabKey TabKeyForNode(const TabCollectionNode* node) {
  if (!node || node->type() != TabCollectionNode::Type::TAB) {
    return LiveTabKey();
  }
  const tabs::ConstChildPtr data = node->GetNodeData();
  if (!std::holds_alternative<const tabs::TabInterface*>(data)) {
    return LiveTabKey();
  }
  return TabStripBridge::KeyForTab(std::get<const tabs::TabInterface*>(data));
}

bool ComputeNodeVisible(const TabCollectionNode* node,
                        const VerticalPresentationFilter& filter,
                        bool fail_open);

bool TabNodeVisible(const TabCollectionNode* node,
                    const VerticalPresentationFilter& filter,
                    bool fail_open) {
  if (!node || node->type() != TabCollectionNode::Type::TAB) {
    return false;
  }
  const LiveTabKey tab = TabKeyForNode(node);
  if (filter.IsExplicitlyHiddenTab(tab)) {
    return false;
  }
  if (fail_open || filter.disabled()) {
    return true;
  }
  return filter.ShouldPresentTab(tab);
}

bool SplitNodeVisible(const TabCollectionNode* node,
                      const VerticalPresentationFilter& filter,
                      bool fail_open) {
  if (!node || node->type() != TabCollectionNode::Type::SPLIT) {
    return false;
  }
  if (fail_open || filter.disabled()) {
    return true;
  }
  int tab_children = 0;
  int visible_tabs = 0;
  for (const auto& child : node->children()) {
    if (child->type() != TabCollectionNode::Type::TAB) {
      continue;
    }
    tab_children++;
    if (TabNodeVisible(child.get(), filter, fail_open)) {
      visible_tabs++;
    }
  }
  return tab_children > 0 && visible_tabs == tab_children;
}

bool GroupNodeVisible(const TabCollectionNode* node,
                      const VerticalPresentationFilter& filter,
                      bool fail_open) {
  if (!node || node->type() != TabCollectionNode::Type::GROUP) {
    return false;
  }
  if (fail_open || filter.disabled()) {
    return true;
  }
  for (const auto& child : node->children()) {
    if (ComputeNodeVisible(child.get(), filter, fail_open)) {
      return true;
    }
  }
  return false;
}

bool ContainerNodeVisible(const TabCollectionNode* node,
                          const VerticalPresentationFilter& filter,
                          bool fail_open) {
  for (const auto& child : node->children()) {
    if (ComputeNodeVisible(child.get(), filter, fail_open)) {
      return true;
    }
  }
  return fail_open || filter.disabled();
}

bool ComputeNodeVisible(const TabCollectionNode* node,
                        const VerticalPresentationFilter& filter,
                        bool fail_open) {
  if (!node || !node->view()) {
    return false;
  }
  // Dispatch tabs through TabNodeVisible even during fail-open/disabled
  // recovery. Explicitly hidden browser-chrome placeholders are stricter than
  // workspace filtering and must never be revived by the recovery fast path.
  switch (node->type()) {
    case TabCollectionNode::Type::TAB:
      return TabNodeVisible(node, filter, fail_open);
    case TabCollectionNode::Type::SPLIT:
      return SplitNodeVisible(node, filter, fail_open);
    case TabCollectionNode::Type::GROUP:
      return GroupNodeVisible(node, filter, fail_open);
    case TabCollectionNode::Type::PINNED:
    case TabCollectionNode::Type::UNPINNED:
    case TabCollectionNode::Type::TABSTRIP:
      return ContainerNodeVisible(node, filter, fail_open);
  }
  return true;
}

void ApplyAccessibility(views::View* view, bool visible) {
  if (!view) {
    return;
  }
  view->GetViewAccessibility().SetIsIgnored(!visible);
}

void ApplyVisibilityRecursive(TabCollectionNode* node,
                              const VerticalPresentationFilter& filter,
                              bool fail_open,
                              views::View* focused_view,
                              views::View** first_visible_tab_view) {
  if (!node) {
    return;
  }
  for (const auto& child : node->children()) {
    ApplyVisibilityRecursive(child.get(), filter, fail_open, focused_view,
                             first_visible_tab_view);
  }
  if (!node->view()) {
    return;
  }
  const bool visible = ComputeNodeVisible(node, filter, fail_open);
  node->view()->SetVisible(visible);
  node->view()->SetCanProcessEventsWithinSubtree(visible);
  ApplyAccessibility(node->view(), visible);
  if (visible && node->type() == TabCollectionNode::Type::TAB &&
      first_visible_tab_view && !*first_visible_tab_view) {
    *first_visible_tab_view = node->view();
  }
  if (!visible && focused_view && node->view()->Contains(focused_view)) {
    views::FocusManager* fm = node->view()->GetFocusManager();
    if (fm) {
      fm->ClearFocus();
    }
  }
}

views::View* FindFirstVisibleTabViewRecursive(
    TabCollectionNode* node,
    const WindowProjection& projection,
    bool fail_open) {
  if (!node) {
    return nullptr;
  }
  VerticalPresentationFilter filter(projection);
  views::View* first = nullptr;
  ApplyVisibilityRecursive(node, filter, fail_open, nullptr, &first);
  return first;
}

}  // namespace

VerticalPresentationAdapter::VerticalPresentationAdapter() = default;
VerticalPresentationAdapter::~VerticalPresentationAdapter() = default;

void VerticalPresentationAdapter::UpdateProjection(
    const WindowProjection& projection) {
  filter_.UpdateProjection(projection);
  fail_open_ = projection.status == ProjectionStatus::kFailOpen;
}

void VerticalPresentationAdapter::SetDisabled(bool disabled) {
  filter_.SetDisabled(disabled);
}

void VerticalPresentationAdapter::EnterFailOpen() {
  fail_open_ = true;
}

void VerticalPresentationAdapter::ClearFailOpen() {
  fail_open_ = false;
}

void VerticalPresentationAdapter::ApplyToVerticalTabStripRegion(
    VerticalTabStripRegionView* region) {
  if (!region) {
    return;
  }
  RootTabCollectionNode* root = region->GetSeoulRootNode();
  if (!root) {
    return;
  }
  views::View* focused = region->GetFocusManager()
                             ? region->GetFocusManager()->GetFocusedView()
                             : nullptr;
  views::View* first_visible = nullptr;
  ApplyVisibilityRecursive(root, filter_, fail_open_, focused, &first_visible);
  if (focused && !focused->GetVisible()) {
    views::FocusManager* fm = region->GetFocusManager();
    if (fm) {
      if (first_visible) {
        first_visible->RequestFocus();
      } else {
        fm->ClearFocus();
      }
    }
  }
  region->InvalidateLayout();
}

views::View* VerticalPresentationAdapter::FindDefaultFocusableChild(
    VerticalTabStripRegionView* region,
    const WindowProjection& projection) {
  if (!region) {
    return nullptr;
  }
  RootTabCollectionNode* root = region->GetSeoulRootNode();
  if (!root) {
    return nullptr;
  }
  const bool fail_open = projection.status == ProjectionStatus::kFailOpen;
  if (projection.active_tab.is_valid() && !fail_open) {
    VerticalPresentationFilter filter(projection);
    views::View* active_view = nullptr;
    std::function<void(TabCollectionNode*)> find_active;
    find_active = [&](TabCollectionNode* node) {
      if (!node || active_view) {
        return;
      }
      if (node->type() == TabCollectionNode::Type::TAB &&
          TabKeyForNode(node) == projection.active_tab && node->view() &&
          ComputeNodeVisible(node, filter, fail_open)) {
        active_view = node->view();
        return;
      }
      for (const auto& child : node->children()) {
        find_active(child.get());
      }
    };
    find_active(root);
    if (active_view) {
      return active_view;
    }
  }
  return FindFirstVisibleTabViewRecursive(root, projection, fail_open);
}

}  // namespace seoul
