// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_BUBBLE_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_BUBBLE_H_

#include <string>

namespace content {
class WebContents;
}

namespace seoul {

// Shows the Boost editor for `web_contents` as a native bubble anchored to its
// window's toolbar.
//
// This is the Arc-shaped flow: you are ON the site, you ask to boost it, and a
// compact popover appears over the page while every change applies live behind
// it. The previous editor lived inside the Canvas WebUI, which meant editing a
// site from a different surface - and a Canvas opened as a tab could never even
// see an http(s) page as "current". The editor now comes to the page.
//
// Returns false when `web_contents` is not boostable (non-http(s), no window,
// no runtime); the caller reports that to the user.
bool ShowBoostBubbleForWebContents(content::WebContents* web_contents);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_BUBBLE_H_
