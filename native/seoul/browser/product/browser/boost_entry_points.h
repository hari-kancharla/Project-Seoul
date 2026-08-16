// Project Seoul Boosts - native discovery entry points.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_

namespace content {
class WebContents;
}

class BrowserWindowInterface;

namespace seoul {

// Shared gate for the site-controls button and omnibox action.
bool CanBoostWebContents(content::WebContents *web_contents);

// The normal browser window hosting `web_contents`, or null when it is not in
// one (app windows, popups, detached contents). Exported because the native
// Boost bubble needs the same answer the side-panel opener used.
BrowserWindowInterface *EligibleBrowserFor(content::WebContents *web_contents);

// Opens Seoul Canvas on Boosts for the current HTTP(S) tab. Unsupported and
// internal pages are rejected without opening an editor on a different site.
bool OpenBoostEditorForWebContents(content::WebContents *web_contents);

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_
