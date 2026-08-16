// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SPACE_PARTITION_RESOLVER_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SPACE_PARTITION_RESOLVER_H_

#include "base/memory/scoped_refptr.h"

class BrowserWindowInterface;
class GURL;

namespace content {
class SiteInstance;
}

namespace seoul {

// The SiteInstance a new tab in `browser` should be created with.
//
// Returns a SiteInstance pinned to the active Space's own StoragePartition when
// that Space is isolated, and null when it is not - null meaning "no opinion",
// so the caller keeps Chromium's ordinary choice rather than being handed a
// default partition that looks deliberate.
//
// Only the tab's *initial* SiteInstance is decided here. Chromium preserves the
// fixed partition across subsequent navigations, and a tab opened from another
// tab inherits its opener's SiteInstance before this is consulted, so a link
// followed out of a container stays inside it.
scoped_refptr<content::SiteInstance> SiteInstanceForNewTabInActiveSpace(
    BrowserWindowInterface* browser,
    const GURL& url);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SPACE_PARTITION_RESOLVER_H_
