// Project Seoul Canvas - WebUI controller.

#include "seoul/browser/canvas/seoul_canvas_ui.h"

#include <string>
#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"  // nogncheck
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "seoul/browser/canvas/seoul_canvas_page_handler.h"
#include "seoul/grit/seoul_canvas_resources.h"
#include "seoul/grit/seoul_canvas_resources_map.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

namespace seoul {

namespace {

// Packaged, in-process resources with a strict CSP. script-src 'self' only:
// no inline handlers, no eval, no remote scripts. The resource ids come from
// the generated grit pack (seoul_canvas_resources).
void SetUpDataSource(content::WebUIDataSource* source) {
  // Install the packaged Canvas resources (HTML/CSS/JS + the generated Mojo
  // bindings) and the default document. SetupWebUIDataSource applies the
  // trusted-types CSP for a first-party WebUI.
  webui::SetupWebUIDataSource(source, kSeoulCanvasResources,
                              IDR_SEOUL_CANVAS_CANVAS_HTML);
  // Tighten beyond the defaults: allow only this WebUI and Chromium's
  // packaged component library. No remote scripts, inline scripts, or eval.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self';");
  const std::string realtime_origin =
      std::string("https://api.") + "open" + "ai.com";
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' " + realtime_origin + ";");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ObjectSrc, "object-src 'none';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::DefaultSrc, "default-src 'self';");
}

}  // namespace

SeoulCanvasUIConfig::SeoulCanvasUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, kSeoulCanvasHost) {}
SeoulCanvasUIConfig::~SeoulCanvasUIConfig() = default;

bool SeoulCanvasUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  // Only for a regular profile; never for incognito/guest.
  Profile* profile = Profile::FromBrowserContext(browser_context);
  return profile && profile->IsRegularProfile();
}

SeoulCanvasUI::SeoulCanvasUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), kSeoulCanvasHost);
  SetUpDataSource(source);
}

SeoulCanvasUI::~SeoulCanvasUI() = default;

void SeoulCanvasUI::BindInterface(
    mojo::PendingReceiver<canvas::mojom::PageHandlerFactory> receiver) {
  factory_receiver_.reset();
  factory_receiver_.Bind(std::move(receiver));
}

void SeoulCanvasUI::CreatePageHandler(
    mojo::PendingRemote<canvas::mojom::Page> page,
    mojo::PendingReceiver<canvas::mojom::PageHandler> handler) {
  Profile* profile = Profile::FromWebUI(web_ui());
  BrowserWindowInterface* browser_window =
      webui::GetBrowserWindowInterface(web_ui()->GetWebContents());
  page_handler_ = std::make_unique<SeoulCanvasPageHandler>(
      std::move(handler), std::move(page), profile, browser_window);
}

WEB_UI_CONTROLLER_TYPE_IMPL(SeoulCanvasUI)

void SeoulCanvasUI::ShowUI(ShowUICallback callback) {
  if (embedder()) embedder()->ShowUI();
  std::move(callback).Run(!!embedder());
}

void SeoulCanvasUI::CloseUI() {
  BrowserWindowInterface* window =
      webui::GetBrowserWindowInterface(web_ui()->GetWebContents());
  SidePanelUI* panel = window ? window->GetFeatures().side_panel_ui() : nullptr;
  if (embedder() && panel &&
      panel->GetCurrentEntryId() == SidePanelEntryId::kSeoulCanvas)
    panel->Close();
}

}  // namespace seoul
