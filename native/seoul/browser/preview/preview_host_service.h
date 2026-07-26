// Project Seoul Chromium Preview host service.
// Owns one visible non-tab Preview bubble per exact browser window and bridges
// its WebContents lifecycle to PreviewManager and LifecycleCoordinator.

#ifndef SEOUL_BROWSER_PREVIEW_PREVIEW_HOST_SERVICE_H_
#define SEOUL_BROWSER_PREVIEW_PREVIEW_HOST_SERVICE_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/preview/preview_types.h"

class Profile;
namespace content {
class WebContents;
}

namespace seoul {

class LifecycleCoordinator;
class PreviewManager;
class SeoulPreviewBubbleView;

struct PreviewPromotionRoute {
  PreviewPromotionRoute();
  PreviewPromotionRoute(const PreviewPromotionRoute &);
  PreviewPromotionRoute(PreviewPromotionRoute &&);
  PreviewPromotionRoute &operator=(const PreviewPromotionRoute &);
  PreviewPromotionRoute &operator=(PreviewPromotionRoute &&);
  ~PreviewPromotionRoute();

  bool allowed = true;
  PreviewPromotionTarget target = PreviewPromotionTarget::kTab;
  WorkspaceId source_workspace;
  WorkspaceId target_workspace;
  std::string blocked_reason;
};

using PreviewPromotionRouteResolver =
    base::RepeatingCallback<PreviewPromotionRoute(const PreviewRecord &,
                                                  PreviewPromotionTarget)>;
using PreviewWorkspaceSwitcher =
    base::RepeatingCallback<bool(LiveWindowKey, WorkspaceId)>;

class PreviewHostService {
public:
  PreviewHostService(Profile *profile, PreviewManager *manager,
                     LifecycleCoordinator *lifecycle,
                     PreviewPromotionRouteResolver route_resolver,
                     PreviewWorkspaceSwitcher workspace_switcher);
  PreviewHostService(const PreviewHostService &) = delete;
  PreviewHostService &operator=(const PreviewHostService &) = delete;
  ~PreviewHostService();

  PreviewResult<PreviewId> Open(LiveWindowKey window, LiveTabKey parent_tab,
                                const GURL &url);
  // User-gesture bridge for link UI such as the page context menu. Resolves
  // the exact owning normal window and parent tab; never uses focus/recency.
  PreviewResult<PreviewId> OpenFromLink(content::WebContents *parent_contents,
                                        const GURL &url);
  // Explicit promotion entry point shared by the native bubble and typed
  // product callers. The Preview remains ready after any pre-transfer failure.
  PreviewStatusResult Promote(const PreviewId &id,
                              PreviewPromotionTarget target);
  size_t DismissForParent(LiveTabKey parent_tab);
  size_t DismissForWindow(LiveWindowKey window);
  void Shutdown();

private:
  void OnViewClosed(PreviewId id, LiveWindowKey window);

  raw_ptr<Profile> profile_;
  raw_ptr<PreviewManager> manager_;
  raw_ptr<LifecycleCoordinator> lifecycle_;
  PreviewPromotionRouteResolver route_resolver_;
  PreviewWorkspaceSwitcher workspace_switcher_;
  std::map<PreviewId, raw_ptr<SeoulPreviewBubbleView>> views_;
  std::map<LiveWindowKey, PreviewId> preview_by_window_;
  bool shutting_down_ = false;
  base::WeakPtrFactory<PreviewHostService> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_PREVIEW_PREVIEW_HOST_SERVICE_H_
