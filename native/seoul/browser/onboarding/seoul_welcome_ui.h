// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_UI_H_
#define SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "seoul/browser/onboarding/welcome.mojom.h"

namespace seoul {

class SeoulWelcomePageHandler;
class SeoulWelcomeUI;

inline constexpr char kSeoulWelcomeHost[] = "seoul-welcome";

class SeoulWelcomeUIConfig
    : public DefaultTopChromeWebUIConfig<SeoulWelcomeUI> {
 public:
  SeoulWelcomeUIConfig();
  ~SeoulWelcomeUIConfig() override;

  // DefaultTopChromeWebUIConfig:
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class SeoulWelcomeUI : public TopChromeWebUIController,
                       public welcome::mojom::PageHandlerFactory {
 public:
  explicit SeoulWelcomeUI(content::WebUI* web_ui);
  SeoulWelcomeUI(const SeoulWelcomeUI&) = delete;
  SeoulWelcomeUI& operator=(const SeoulWelcomeUI&) = delete;
  ~SeoulWelcomeUI() override;

  void BindInterface(
      mojo::PendingReceiver<welcome::mojom::PageHandlerFactory> receiver);

  // welcome::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingReceiver<welcome::mojom::PageHandler> handler) override;

  static constexpr std::string_view GetWebUIName() { return "SeoulWelcome"; }

 private:
  std::unique_ptr<SeoulWelcomePageHandler> page_handler_;
  mojo::Receiver<welcome::mojom::PageHandlerFactory> factory_receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_ONBOARDING_SEOUL_WELCOME_UI_H_
