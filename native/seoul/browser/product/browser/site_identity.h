// Project Seoul site identity - forgetting a site.
//
// "Forget this site" is Tor's New Identity scoped to one site and made
// complete: everything the site's registrable domain stored in this profile -
// cookies, storage, cache, its own permissions - is removed, the site's
// farbling identity rotates so its fingerprint of this machine stops matching
// too, and the page reloads as a first visit.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace seoul {

// Forgets the site `web_contents` is showing. `done` runs once the removal has
// finished and the reload has been issued. Returns false, running nothing,
// when there is no http(s) site to forget.
bool ForgetSite(content::WebContents* web_contents, base::OnceClosure done);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_
