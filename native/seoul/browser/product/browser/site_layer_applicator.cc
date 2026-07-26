// Project Seoul Site Layers - live WebContents application.

#include "seoul/browser/product/browser/site_layer_applicator.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/product/browser/boost_web_preferences.h"
#include "seoul/browser/site_layers/site_layer_compiler.h"
#include "seoul/browser/site_layers/site_layer_registry.h"
#include "url/origin.h"

namespace seoul {

namespace {

bool IsCustomizableUrl(const GURL &url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

std::u16string BuildApplyScript(const std::string &css, bool tint_enabled) {
  // JSONWriter is the only interpolation boundary. CSS has already passed the
  // Site Layer compiler, and the encoded JSON string is passed only to
  // CSSStyleSheet.replaceSync; it is never parsed or evaluated as script.
  base::DictValue payload;
  payload.Set("css", css);
  payload.Set("tint", tint_enabled);
  std::string payload_json;
  base::JSONWriter::Write(payload, &payload_json);
  const std::string script =
      "(() => {"
      "const p=" +
      payload_json +
      ";"
      "const k='__seoulSiteLayerSheetV1';"
      "const tk='__seoulBoostTintElementV1';"
      "const old=globalThis[k];"
      "if(old){document.adoptedStyleSheets="
      "document.adoptedStyleSheets.filter(s=>s!==old);delete globalThis[k];}"
      "let tint=globalThis[tk];"
      "if(tint&&!tint.isConnected){delete globalThis[tk];tint=null;}"
      "if(!p.tint){if(tint)tint.remove();delete globalThis[tk];}"
      "else if(!tint){tint=document.createElement('div');"
      "tint.setAttribute('data-seoul-browser-boost-tint-v1','');"
      "tint.setAttribute('aria-hidden','true');"
      "(document.documentElement||document).appendChild(tint);"
      "globalThis[tk]=tint;}"
      "if(!p.css)return true;"
      "const sheet=new CSSStyleSheet();"
      "sheet.replaceSync(p.css);"
      "document.adoptedStyleSheets=[...document.adoptedStyleSheets,sheet];"
      "globalThis[k]=sheet;"
      "return document.adoptedStyleSheets.includes(sheet);"
      "})()";
  return base::UTF8ToUTF16(script);
}

std::u16string BuildZapScript() {
  // The script is constant: no page data or user-authored strings are
  // interpolated. It runs in Chrome's isolated world, captures input before
  // the page, and returns only a bounded selector assembled from conservative
  // ASCII identifiers plus :nth-of-type(N).
  return uR"JS(
    (() => {
      const key = '__seoulBoostZapV1';
      const resultKey = '__seoulBoostZapResultV1';
      if (globalThis[key]?.cancel) globalThis[key].cancel();
      delete globalThis[resultKey];
      let done = false;
      const overlay = document.createElement('div');
      overlay.setAttribute('data-seoul-boost-zap', '');
      Object.assign(overlay.style, {
        position: 'fixed',
        display: 'none',
        pointerEvents: 'none',
        zIndex: '2147483647',
        border: '2px solid #8ab4f8',
        borderRadius: '4px',
        background: 'rgba(138,180,248,.16)',
        boxShadow: '0 0 0 1px rgba(0,0,0,.28)',
      });
      (document.documentElement || document).appendChild(overlay);
      const captureTypes =
          ['pointerdown', 'mousedown', 'touchstart', 'contextmenu'];
      const suppress = event => {
        event.preventDefault();
        event.stopImmediatePropagation();
      };
      const clean = value => {
        if (done) return;
        done = true;
        document.removeEventListener('pointermove', move, true);
        document.removeEventListener('click', choose, true);
        document.removeEventListener('keydown', keydown, true);
        for (const type of captureTypes)
          document.removeEventListener(type, suppress, true);
        overlay.remove();
        delete globalThis[key];
        globalThis[resultKey] = {done: true, selector: value};
      };
      const safeIdentifier = value =>
          /^[A-Za-z_][A-Za-z0-9_-]*$/.test(value);
      const selectableElement = candidate => {
        let element = candidate instanceof Element ? candidate : null;
        while (element && element.getRootNode() instanceof ShadowRoot)
          element = element.getRootNode().host;
        return element;
      };
      const selectorFor = start => {
        if (start.id && safeIdentifier(start.id)) {
          const byId = '#' + start.id;
          if (document.querySelectorAll(byId).length === 1) return byId;
        }
        const parts = [];
        let element = start;
        for (let depth = 0; element && element.nodeType === 1 &&
             depth < 8; ++depth, element = element.parentElement) {
          let part = element.localName;
          if (!part || !safeIdentifier(part)) return '';
          const classes = Array.from(element.classList)
              .filter(safeIdentifier).slice(0, 2);
          if (classes.length) part += '.' + classes.join('.');
          const parent = element.parentElement;
          if (parent) {
            const sameTag = Array.from(parent.children)
                .filter(child => child.localName === element.localName);
            if (sameTag.length > 1)
              part += ':nth-of-type(' + (sameTag.indexOf(element) + 1) + ')';
          }
          parts.unshift(part);
          const candidate = parts.join(' > ');
          if (candidate.length <= 256) {
            try {
              if (document.querySelectorAll(candidate).length === 1)
                return candidate;
            } catch (_) {}
          }
          if (element === document.documentElement) break;
        }
        return '';
      };
      const move = event => {
        const element = selectableElement(event.composedPath()[0]);
        if (!element || element === overlay) {
          overlay.style.display = 'none';
          return;
        }
        const rect = element.getBoundingClientRect();
        if (!rect.width && !rect.height) {
          overlay.style.display = 'none';
          return;
        }
        Object.assign(overlay.style, {
          display: 'block',
          left: rect.left + 'px',
          top: rect.top + 'px',
          width: rect.width + 'px',
          height: rect.height + 'px',
        });
      };
      const choose = event => {
        suppress(event);
        const element = selectableElement(event.composedPath()[0]);
        clean(element ? selectorFor(element) : '');
      };
      const keydown = event => {
        if (event.key !== 'Escape') return;
        suppress(event);
        clean('');
      };
      globalThis[key] = {cancel: () => clean('')};
      document.addEventListener('pointermove', move, true);
      document.addEventListener('click', choose, true);
      document.addEventListener('keydown', keydown, true);
      for (const type of captureTypes)
        document.addEventListener(type, suppress, true);
      return true;
    })()
  )JS";
}

std::u16string BuildReadZapResultScript() {
  return uR"JS(
    (() => {
      const key = '__seoulBoostZapResultV1';
      const result = globalThis[key];
      if (!result) return {done: false};
      delete globalThis[key];
      return result;
    })()
  )JS";
}

} // namespace

SiteLayerApplicator::SiteLayerApplicator(content::WebContents *web_contents,
                                         SiteLayerRegistry *registry)
    : content::WebContentsObserver(web_contents), registry_(registry) {}

SiteLayerApplicator::~SiteLayerApplicator() {
  CancelZap();
  SetBoostAutomaticDarkMode(web_contents(), false);
}

void SiteLayerApplicator::Refresh(const std::string &scene_id) {
  scene_id_ = scene_id;
  compiled_css_.clear();
  tint_enabled_ = false;
  bool automatic_dark_mode = false;
  content::WebContents *contents = web_contents();
  if (registry_ && contents &&
      IsCustomizableUrl(contents->GetLastCommittedURL())) {
    const url::Origin origin =
        url::Origin::Create(contents->GetLastCommittedURL());
    if (!origin.opaque()) {
      SiteLayerResult<std::string> compiled =
          registry_->CompileForOrigin(origin.Serialize(), scene_id_);
      if (compiled.has_value()) {
        compiled_css_ = std::move(compiled.value());
      }
      automatic_dark_mode = registry_->HasEnabledAdjustmentForOrigin(
          origin.Serialize(), scene_id_,
          SiteAdjustmentKind::kAutomaticDarkMode);
      tint_enabled_ = registry_->HasEnabledAdjustmentForOrigin(
          origin.Serialize(), scene_id_, SiteAdjustmentKind::kTintColor);
    }
  }
  automatic_dark_mode_enabled_ = automatic_dark_mode;
  SetBoostAutomaticDarkMode(contents, automatic_dark_mode_enabled_);
  ApplyToPrimaryMainFrame();
}

void SiteLayerApplicator::BeginZap(ZapCallback callback) {
  CancelZap();
  content::WebContents *contents = web_contents();
  content::RenderFrameHost *frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive() ||
      !IsCustomizableUrl(contents->GetLastCommittedURL())) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  zap_callback_ = std::move(callback);
  const uint64_t generation = ++zap_generation_;
  frame->ExecuteJavaScriptInIsolatedWorld(
      BuildZapScript(),
      base::BindOnce(&SiteLayerApplicator::OnZapInstalled,
                     weak_factory_.GetWeakPtr(), generation),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SiteLayerApplicator::DidFinishNavigation(
    content::NavigationHandle *navigation_handle) {
  if (!navigation_handle || !navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    return;
  }
  CancelZap();
  Refresh(scene_id_);
}

void SiteLayerApplicator::DOMContentLoaded(
    content::RenderFrameHost *render_frame_host) {
  if (!render_frame_host || !render_frame_host->IsInPrimaryMainFrame()) {
    return;
  }
  // Reapply once the head exists. This also repairs pages that replace their
  // head during early boot without waiting for another navigation.
  ApplyToPrimaryMainFrame();
}

void SiteLayerApplicator::WebContentsDestroyed() {
  CancelZap();
  Observe(nullptr);
  compiled_css_.clear();
  tint_enabled_ = false;
  automatic_dark_mode_enabled_ = false;
}

void SiteLayerApplicator::CancelZap() {
  ++zap_generation_;
  if (zap_callback_) {
    std::move(zap_callback_).Run(std::nullopt);
  }
  content::WebContents *contents = web_contents();
  content::RenderFrameHost *frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      u"(() => { globalThis.__seoulBoostZapV1?.cancel?.(); "
      u"delete globalThis.__seoulBoostZapResultV1; return true; })()",
      base::DoNothing(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SiteLayerApplicator::OnZapInstalled(uint64_t generation,
                                         base::Value result) {
  if (generation != zap_generation_ || !zap_callback_) {
    return;
  }
  if (!result.is_bool() || !result.GetBool()) {
    std::move(zap_callback_).Run(std::nullopt);
    return;
  }
  PollZap(generation);
}

void SiteLayerApplicator::PollZap(uint64_t generation) {
  if (generation != zap_generation_ || !zap_callback_) {
    return;
  }
  content::WebContents *contents = web_contents();
  content::RenderFrameHost *frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    std::move(zap_callback_).Run(std::nullopt);
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      BuildReadZapResultScript(),
      base::BindOnce(&SiteLayerApplicator::OnZapPoll,
                     weak_factory_.GetWeakPtr(), generation),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void SiteLayerApplicator::OnZapPoll(uint64_t generation, base::Value result) {
  if (generation != zap_generation_ || !zap_callback_) {
    return;
  }
  if (!result.is_dict()) {
    std::move(zap_callback_).Run(std::nullopt);
    return;
  }
  const base::DictValue &dict = result.GetDict();
  if (!dict.FindBool("done").value_or(false)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SiteLayerApplicator::PollZap,
                       weak_factory_.GetWeakPtr(), generation),
        base::Milliseconds(40));
    return;
  }
  std::optional<std::string> selector;
  const std::string *value = dict.FindString("selector");
  if (value && IsSafeSelector(*value)) {
    selector = *value;
  }
  std::move(zap_callback_).Run(std::move(selector));
}

void SiteLayerApplicator::ApplyToPrimaryMainFrame() {
  content::WebContents *contents = web_contents();
  if (!contents) {
    return;
  }
  content::RenderFrameHost *frame = contents->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      BuildApplyScript(compiled_css_, tint_enabled_), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

} // namespace seoul
