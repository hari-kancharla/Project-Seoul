// Project Seoul site identity - forgetting a site.
//
// Removes filterable browsing data for the site's registrable domain and
// rotates its farbling identity only after successful removal.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_

#include <cstdint>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace seoul {

// Starts removal for the site `web_contents` is showing. `done` receives the
// failed data-type mask (zero on success). Only a successful removal rotates
// identity and reloads, and only the original document may be reloaded.
// Returns false, without calling `done`, when removal cannot be started.
bool ForgetSite(content::WebContents* web_contents,
                base::OnceCallback<void(uint64_t)> done);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SITE_IDENTITY_H_
