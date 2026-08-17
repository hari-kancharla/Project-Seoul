// Project Seoul Site Layers - live WebContents application.
//
// A SiteLayerApplicator is owned by the profile runtime for one live tab. It
// compiles the profile's validated declarative layers for the tab's committed
// origin and installs the resulting CSS through one fixed, reviewed isolated-
// world script as a browser-owned constructable sheet. Layer content never
// becomes executable JavaScript, and constructable sheets are not weakened by
// or exceptions to the page's own CSP. Navigation replaces the document, so
// the applicator reapplies after every primary-main-frame commit and
// DOMContentLoaded. Clearing or disabling the matching layers removes Seoul's
// sheet immediately.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SITE_LAYER_APPLICATOR_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SITE_LAYER_APPLICATOR_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
class WebContents;
} // namespace content

namespace seoul {

class SiteLayerRegistry;

class SiteLayerApplicator : public content::WebContentsObserver {
public:
  using ZapCallback =
      base::OnceCallback<void(std::optional<std::string> selector)>;

  SiteLayerApplicator(content::WebContents *web_contents,
                      SiteLayerRegistry *registry);
  SiteLayerApplicator(const SiteLayerApplicator &) = delete;
  SiteLayerApplicator &operator=(const SiteLayerApplicator &) = delete;
  ~SiteLayerApplicator() override;

  // Recompiles against the current committed origin and replaces the applied
  // stylesheet. `scene_id` is empty for globally scoped browsing.
  void Refresh(const std::string &scene_id);

  // The most recent successfully compiled CSS, exposed only so the integration
  // layer can report truthful state and browser tests can assert rollback.
  const std::string &compiled_css_for_testing() const { return compiled_css_; }
  bool IsAttachedTo(content::WebContents *contents) const {
    return web_contents() == contents;
  }
  bool automatic_dark_mode_enabled_for_testing() const {
    return automatic_dark_mode_enabled_;
  }

  // Starts one browser-owned click-to-select session in the primary page.
  // Events are suppressed in capture phase until a selection or Escape. The
  // resulting selector is still revalidated by SiteLayerCompiler before it is
  // persisted. Starting another session cancels the previous one.
  void BeginZap(ZapCallback callback);
  void CancelZap();

  // content::WebContentsObserver:
  void
  DidFinishNavigation(content::NavigationHandle *navigation_handle) override;
  void DOMContentLoaded(content::RenderFrameHost *render_frame_host) override;
  void WebContentsDestroyed() override;

private:
  void OnZapInstalled(uint64_t generation, base::Value result);
  void PollZap(uint64_t generation);
  void OnZapPoll(uint64_t generation, base::Value result);
  void ApplyToPrimaryMainFrame();

  raw_ptr<SiteLayerRegistry> registry_;
  std::string scene_id_;
  std::string compiled_css_;
  std::string custom_javascript_;
  bool tint_enabled_ = false;
  bool automatic_dark_mode_enabled_ = false;
  uint64_t zap_generation_ = 0;
  ZapCallback zap_callback_;
  base::WeakPtrFactory<SiteLayerApplicator> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_SITE_LAYER_APPLICATOR_H_
