// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_CODE_DIALOG_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_CODE_DIALOG_H_

#include <string>

#include "content/public/browser/weak_document_ptr.h"

namespace seoul {

// Called after the native Boost bubble closes. Revalidates the original
// document before opening a draft editor; an empty id creates a new Boost.
void ShowBoostCodeDialog(content::WeakDocumentPtr source, std::string layer_id);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_BOOST_CODE_DIALOG_H_
