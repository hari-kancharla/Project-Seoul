// Project Seoul Shields panel.
// Brave's Shields, as a Seoul surface: the per-site face of the native
// blocker. One panel anchored to the address field showing what the blocker
// did on this page and holding the only three decisions a person makes about
// it - off for this site, Standard, or Aggressive. The backend has carried
// per-site modes, temporary disables, and per-frame blocked counts since the
// blocker landed; this panel is those switches finally reachable.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SHIELDS_BUBBLE_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SHIELDS_BUBBLE_H_

namespace content {
class WebContents;
}  // namespace content

namespace seoul {

// Opens the Shields panel for `web_contents`, anchored to its window's
// address field. Returns false when the tab cannot carry shields (no window,
// non-web scheme) - the caller treats that as "the control should not have
// been reachable".
bool ShowShieldsBubbleForWebContents(content::WebContents* web_contents);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_SHIELDS_BUBBLE_H_
