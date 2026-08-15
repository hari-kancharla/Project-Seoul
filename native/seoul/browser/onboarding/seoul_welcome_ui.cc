// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/onboarding/seoul_welcome_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "seoul/browser/onboarding/seoul_welcome_page_handler.h"
#include "seoul/grit/seoul_welcome_resources.h"
#include "seoul/grit/seoul_welcome_resources_map.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

namespace seoul {

namespace {

// Packaged, in-process resources under a strict CSP.
//
// Tighter than the Canvas: the welcome surface talks to nothing off-device, so
// connect-src is 'self' with no exceptions at all. A first-run screen is the
// least appropriate place in the product to reach the network.
void SetUpDataSource(content::WebUIDataSource* source) {
  webui::SetupWebUIDataSource(source, kSeoulWelcomeResources,
                              IDR_SEOUL_WELCOME_WELCOME_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc, "connect-src 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ObjectSrc, "object-src 'none';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::DefaultSrc, "default-src 'self';");
}

}  // namespace

SeoulWelcomeUIConfig::SeoulWelcomeUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  kSeoulWelcomeHost) {}
SeoulWelcomeUIConfig::~SeoulWelcomeUIConfig() = default;

bool SeoulWelcomeUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  // Regular profiles only. Onboarding writes profile prefs and offers to change
  // the system default browser; neither belongs to an incognito or guest
  // session, which by definition is not where a person sets the product up.
  Profile* profile = Profile::FromBrowserContext(browser_context);
  return profile && profile->IsRegularProfile();
}

SeoulWelcomeUI::SeoulWelcomeUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), kSeoulWelcomeHost);
  SetUpDataSource(source);
}

SeoulWelcomeUI::~SeoulWelcomeUI() = default;

void SeoulWelcomeUI::BindInterface(
    mojo::PendingReceiver<welcome::mojom::PageHandlerFactory> receiver) {
  factory_receiver_.reset();
  factory_receiver_.Bind(std::move(receiver));
}

void SeoulWelcomeUI::CreatePageHandler(
    mojo::PendingReceiver<welcome::mojom::PageHandler> handler) {
  Profile* profile = Profile::FromWebUI(web_ui());
  BrowserWindowInterface* browser_window =
      webui::GetBrowserWindowInterface(web_ui()->GetWebContents());
  page_handler_ = std::make_unique<SeoulWelcomePageHandler>(
      std::move(handler), profile, browser_window);
}

WEB_UI_CONTROLLER_TYPE_IMPL(SeoulWelcomeUI)

}  // namespace seoul
