// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
//
// Arc's Capture: drag a rectangle over the live page and keep that region as a
// picture you can put on an Easel. Seoul's authored spatial surface is Boards,
// and a capture lands in the Library as a kCapture artifact that a Board can
// reference, so the picture has one owner and the Board holds a reference to
// it rather than a second copy of the bytes.
//
// The region is chosen by the user in the page, but the pixels are read in the
// browser process from the compositor surface - the page never hands us image
// data, so a page cannot fabricate a capture of something it does not show.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/files/file_path.h"
#include "base/values.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

class SkBitmap;

namespace seoul {

// The finished capture: where the file landed and what it was taken from.
struct CaptureResult {
  CaptureResult();
  CaptureResult(const CaptureResult&);
  CaptureResult(CaptureResult&&);
  CaptureResult& operator=(const CaptureResult&);
  CaptureResult& operator=(CaptureResult&&);
  ~CaptureResult();

  std::string file_path;
  std::string origin;
  std::string title;
  int width = 0;
  int height = 0;
};

// Runs one capture against one page. Deleting it before completion cancels the
// selection and removes the overlay.
class SeoulCapture : public content::WebContentsObserver {
 public:
  // Runs with the result, or with nothing when the user cancelled, the page
  // went away, or the region was too small to be a deliberate selection.
  using CaptureCallback =
      base::OnceCallback<void(std::optional<CaptureResult>)>;

  SeoulCapture(content::WebContents* web_contents,
               const base::FilePath& capture_directory);
  SeoulCapture(const SeoulCapture&) = delete;
  SeoulCapture& operator=(const SeoulCapture&) = delete;
  ~SeoulCapture() override;

  // Puts the selection overlay on the page. Only one runs at a time.
  void BeginRegion(CaptureCallback callback);
  // Arc's "Capture Full Page": no selection, the whole visible page.
  void CaptureFullPage(CaptureCallback callback);

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;
  void PrimaryPageChanged(content::Page& page) override;

 private:
  void PollSelection(uint64_t generation);
  void OnSelectionPolled(uint64_t generation, base::Value result);
  void CopyRegion(const gfx::Rect& region_in_css_pixels);
  void OnCopied(const content::CopyFromSurfaceResult& result);
  void FinishWith(std::optional<CaptureResult> result);
  void Cancel();

  const base::FilePath capture_directory_;
  CaptureCallback callback_;
  // Bumped by every begin and every cancel, so a reply from an abandoned
  // selection is recognised and dropped rather than answering the new one.
  uint64_t generation_ = 0;
  base::WeakPtrFactory<SeoulCapture> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_CAPTURE_H_
