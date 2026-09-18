// Project Seoul Canvas - WebUI controller.
//
// Serves the first-party Canvas WebUI at chrome://seoul-canvas inside a native
// Views side panel and binds the Mojo PageHandlerFactory. The data source is
// packaged, in-process resources with a strict CSP: scripts are limited to
// this WebUI and Chromium's packaged component library, with no remote
// scripts, inline scripts, or eval. This is Chromium-facing glue; it compiles
// only on a capable host. The integration patch registers the WebUIConfig and
// one kSeoulCanvas SidePanelEntry per regular browser window; the native Shell
// command launcher opens that exact window-bound entry.

#ifndef SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_UI_H_
#define SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "seoul/browser/canvas/canvas.mojom.h"

namespace seoul {

class SeoulCanvasPageHandler;
class SeoulCanvasUI;

inline constexpr char kSeoulCanvasHost[] = "seoul-canvas";

// WebUIConfig so the WebUI system can construct the controller for the host.
class SeoulCanvasUIConfig : public DefaultTopChromeWebUIConfig<SeoulCanvasUI> {
 public:
  SeoulCanvasUIConfig();
  ~SeoulCanvasUIConfig() override;

  // DefaultTopChromeWebUIConfig:
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class SeoulCanvasUI : public TopChromeWebUIController,
                      public canvas::mojom::PageHandlerFactory {
 public:
  explicit SeoulCanvasUI(content::WebUI* web_ui);
  SeoulCanvasUI(const SeoulCanvasUI&) = delete;
  SeoulCanvasUI& operator=(const SeoulCanvasUI&) = delete;
  ~SeoulCanvasUI() override;

  // Instantiated by the mojo bindings when the renderer requests the factory.
  void BindInterface(
      mojo::PendingReceiver<canvas::mojom::PageHandlerFactory> receiver);

  // canvas::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<canvas::mojom::Page> page,
      mojo::PendingReceiver<canvas::mojom::PageHandler> handler) override;
  void ShowUI(ShowUICallback callback) override;
  void CloseUI() override;

  static constexpr std::string_view GetWebUIName() { return "SeoulCanvas"; }

 private:
  std::unique_ptr<SeoulCanvasPageHandler> page_handler_;
  mojo::Receiver<canvas::mojom::PageHandlerFactory> factory_receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_UI_H_
