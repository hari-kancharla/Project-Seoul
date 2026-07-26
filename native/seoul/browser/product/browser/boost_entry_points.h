// Project Seoul Boosts - native discovery entry points.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_

namespace content {
class WebContents;
}

namespace seoul {

// Shared gate for the site-controls button and omnibox action.
bool CanBoostWebContents(content::WebContents *web_contents);

// Opens Seoul Canvas on Boosts for the current tab. HTTP(S) pages open a fresh
// live editor; internal pages open the explicit "open a website" state instead
// of silently falling through to a search/navigation.
bool OpenBoostEditorForWebContents(content::WebContents *web_contents);

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_ENTRY_POINTS_H_
