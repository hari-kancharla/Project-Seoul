// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_capture.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

namespace seoul {
namespace {

// How often the browser asks the page whether the drag finished. The selection
// is driven by pointer events in the page, and this is the same poll shape the
// element picker already uses rather than a second mechanism.
constexpr base::TimeDelta kPollInterval = base::Milliseconds(80);

// Below this a drag is a stray click, not a region someone meant to capture.
constexpr int kMinimumRegionEdge = 8;

// A surface copy that has not answered by now is not going to; the user gets
// a clear failure instead of a capture that never resolves.
constexpr base::TimeDelta kCopyTimeout = base::Seconds(5);

// Constant script: no page data and no user text is interpolated. It runs in
// Chrome's isolated world, draws its own overlay, takes pointer input before
// the page can, and reports only four numbers.
std::u16string BuildRegionScript() {
  return uR"JS(
    (() => {
      const key = '__seoulCaptureV1';
      const resultKey = '__seoulCaptureResultV1';
      if (globalThis[key]) { globalThis[key].cancel(); }
      const overlay = document.createElement('div');
      Object.assign(overlay.style, {
        position: 'fixed', inset: '0', zIndex: '2147483647',
        cursor: 'crosshair', background: 'rgba(23,23,19,0.18)',
      });
      const marquee = document.createElement('div');
      Object.assign(marquee.style, {
        position: 'fixed', border: '1px solid #fff', display: 'none',
        background: 'rgba(255,255,255,0.12)', pointerEvents: 'none',
        boxShadow: '0 0 0 9999px rgba(23,23,19,0.28)',
      });
      overlay.appendChild(marquee);
      (document.documentElement || document).appendChild(overlay);

      let startX = 0, startY = 0, dragging = false;
      const finish = (value) => {
        globalThis[resultKey] = value;
        cleanup();
      };
      const cleanup = () => {
        overlay.remove();
        window.removeEventListener('keydown', onKey, true);
        delete globalThis[key];
      };
      const onKey = (event) => {
        if (event.key === 'Escape') {
          event.preventDefault();
          event.stopPropagation();
          finish({cancelled: true});
        }
      };
      overlay.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        event.stopPropagation();
        dragging = true;
        startX = event.clientX; startY = event.clientY;
        Object.assign(marquee.style, {
          display: 'block', left: startX + 'px', top: startY + 'px',
          width: '0px', height: '0px',
        });
      }, true);
      overlay.addEventListener('pointermove', (event) => {
        if (!dragging) { return; }
        const x = Math.min(startX, event.clientX);
        const y = Math.min(startY, event.clientY);
        Object.assign(marquee.style, {
          left: x + 'px', top: y + 'px',
          width: Math.abs(event.clientX - startX) + 'px',
          height: Math.abs(event.clientY - startY) + 'px',
        });
      }, true);
      overlay.addEventListener('pointerup', (event) => {
        if (!dragging) { return; }
        event.preventDefault();
        event.stopPropagation();
        dragging = false;
        finish({
          x: Math.min(startX, event.clientX),
          y: Math.min(startY, event.clientY),
          width: Math.abs(event.clientX - startX),
          height: Math.abs(event.clientY - startY),
        });
      }, true);
      window.addEventListener('keydown', onKey, true);
      globalThis[key] = {cancel: () => finish({cancelled: true})};
      return true;
    })()
  )JS";
}

std::u16string BuildPollScript() {
  return uR"JS(
    (() => {
      const resultKey = '__seoulCaptureResultV1';
      const value = globalThis[resultKey];
      if (value === undefined) { return null; }
      delete globalThis[resultKey];
      return value;
    })()
  )JS";
}

// Encoding and the disk write happen off the UI thread; the caller is answered
// back on it.
std::optional<std::string> WritePng(base::FilePath directory,
                                    SkBitmap bitmap) {
  std::optional<std::vector<uint8_t>> encoded =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (!encoded.has_value() || encoded->empty()) {
    return std::nullopt;
  }
  if (!base::CreateDirectory(directory)) {
    return std::nullopt;
  }
  const base::FilePath path = directory.AppendASCII(
      "capture-" + base::Uuid::GenerateRandomV4().AsLowercaseString() + ".png");
  if (!base::WriteFile(path, *encoded)) {
    return std::nullopt;
  }
  return path.AsUTF8Unsafe();
}

}  // namespace

CaptureResult::CaptureResult() = default;
CaptureResult::CaptureResult(const CaptureResult&) = default;
CaptureResult::CaptureResult(CaptureResult&&) = default;
CaptureResult& CaptureResult::operator=(const CaptureResult&) = default;
CaptureResult& CaptureResult::operator=(CaptureResult&&) = default;
CaptureResult::~CaptureResult() = default;

SeoulCapture::SeoulCapture(content::WebContents* web_contents,
                           const base::FilePath& capture_directory)
    : content::WebContentsObserver(web_contents),
      capture_directory_(capture_directory) {}

SeoulCapture::~SeoulCapture() = default;

void SeoulCapture::BeginRegion(CaptureCallback callback) {
  Cancel();
  content::WebContents* const contents = web_contents();
  content::RenderFrameHost* const frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  callback_ = std::move(callback);
  const uint64_t generation = ++generation_;
  frame->ExecuteJavaScriptInIsolatedWorld(
      BuildRegionScript(),
      base::BindOnce(
          [](base::WeakPtr<SeoulCapture> capture, uint64_t generation,
             base::Value result) {
            if (!capture || generation != capture->generation_) {
              return;
            }
            if (!result.is_bool() || !result.GetBool()) {
              capture->FinishWith(std::nullopt);
              return;
            }
            capture->PollSelection(generation);
          },
          weak_factory_.GetWeakPtr(), generation),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SeoulCapture::CaptureFullPage(CaptureCallback callback) {
  Cancel();
  content::WebContents* const contents = web_contents();
  content::RenderWidgetHostView* const view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  callback_ = std::move(callback);
  ++generation_;
  // An empty source rect asks for the whole visible surface.
  CopyRegion(gfx::Rect());
}

void SeoulCapture::PollSelection(uint64_t generation) {
  if (generation != generation_ || !callback_) {
    return;
  }
  content::WebContents* const contents = web_contents();
  content::RenderFrameHost* const frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    FinishWith(std::nullopt);
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      BuildPollScript(),
      base::BindOnce(&SeoulCapture::OnSelectionPolled,
                     weak_factory_.GetWeakPtr(), generation),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SeoulCapture::OnSelectionPolled(uint64_t generation, base::Value result) {
  if (generation != generation_ || !callback_) {
    return;
  }
  if (result.is_none()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SeoulCapture::PollSelection,
                       weak_factory_.GetWeakPtr(), generation),
        kPollInterval);
    return;
  }
  const base::DictValue* const selection = result.GetIfDict();
  if (!selection || selection->FindBool("cancelled").value_or(false)) {
    FinishWith(std::nullopt);
    return;
  }
  const int x = selection->FindInt("x").value_or(0);
  const int y = selection->FindInt("y").value_or(0);
  const int width = selection->FindInt("width").value_or(0);
  const int height = selection->FindInt("height").value_or(0);
  if (width < kMinimumRegionEdge || height < kMinimumRegionEdge) {
    // A stray click, not a region. Answering with nothing is honest; a
    // one-pixel picture would not be.
    FinishWith(std::nullopt);
    return;
  }
  CopyRegion(gfx::Rect(x, y, width, height));
}

void SeoulCapture::CopyRegion(const gfx::Rect& region_in_css_pixels) {
  content::WebContents* const contents = web_contents();
  content::RenderWidgetHostView* const view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    FinishWith(std::nullopt);
    return;
  }
  // Read from the compositor surface in the browser process. The page chose
  // the rectangle; it never supplies the pixels.
  view->CopyFromSurface(region_in_css_pixels, region_in_css_pixels.size(),
                        kCopyTimeout,
                        base::BindOnce(&SeoulCapture::OnCopied,
                                       weak_factory_.GetWeakPtr()));
}

void SeoulCapture::OnCopied(const content::CopyFromSurfaceResult& copied) {
  if (!callback_) {
    return;
  }
  if (!copied.has_value() || copied->bitmap.drawsNothing()) {
    // The compositor could not produce the region. Reporting nothing is the
    // honest answer; an empty picture is not a capture.
    FinishWith(std::nullopt);
    return;
  }
  const SkBitmap& bitmap = copied->bitmap;
  content::WebContents* const contents = web_contents();
  CaptureResult result;
  result.width = bitmap.width();
  result.height = bitmap.height();
  if (contents) {
    const GURL url = contents->GetLastCommittedURL();
    result.origin = url.DeprecatedGetOriginAsURL().spec();
    result.title = base::UTF16ToUTF8(contents->GetTitle());
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WritePng, capture_directory_, bitmap),
      base::BindOnce(
          [](base::WeakPtr<SeoulCapture> capture, CaptureResult result,
             std::optional<std::string> path) {
            if (!capture) {
              return;
            }
            if (!path.has_value()) {
              capture->FinishWith(std::nullopt);
              return;
            }
            result.file_path = std::move(*path);
            capture->FinishWith(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(result)));
}

void SeoulCapture::FinishWith(std::optional<CaptureResult> result) {
  if (callback_) {
    std::move(callback_).Run(std::move(result));
  }
}

void SeoulCapture::Cancel() {
  ++generation_;
  if (callback_) {
    std::move(callback_).Run(std::nullopt);
  }
  content::WebContents* const contents = web_contents();
  content::RenderFrameHost* const frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      u"(() => { globalThis.__seoulCaptureV1?.cancel?.(); "
      u"delete globalThis.__seoulCaptureResultV1; return true; })()",
      base::DoNothing(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SeoulCapture::WebContentsDestroyed() {
  Cancel();
  Observe(nullptr);
}

void SeoulCapture::PrimaryPageChanged(content::Page& page) {
  // A capture belongs to the page it was started on. Navigating away cancels
  // it rather than silently photographing a different document.
  Cancel();
}

}  // namespace seoul
