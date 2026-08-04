// Project Seoul asynchronous, CSS-only cosmetic filtering agent.

#include "seoul/renderer/cosmetic_filter_agent.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "gin/converter.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_local_frame_client.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-local-handle.h"
#include "v8/include/v8-value.h"

namespace seoul::renderer {
namespace {

constexpr char kSecurityOrigin[] = "chrome://seoul-cosmetic-filters";
constexpr base::TimeDelta kPollInterval = base::Milliseconds(250);
constexpr size_t kMaxIdentifiersPerBatch = 256;
constexpr size_t kMaxIdentifierLength = 256;
constexpr size_t kMaxSelectors = 4096;
constexpr size_t kMaxSelectorLength = 2048;
constexpr size_t kMaxStyleSheetBytes = 512 * 1024;
constexpr size_t kMaxIsolatedScriptBytes = 64 * 1024;
constexpr size_t kMaxProceduralActions = 64;
constexpr size_t kMaxProceduralActionBytes = 4096;
constexpr size_t kMaxProceduralPayloadBytes = 128 * 1024;

constexpr char kInstallDiscoveryScript[] = R"JS(
(() => {
  const key = '__seoulCosmeticFilterState';
  if (globalThis[key]) return;
  const maxPending = 1024;
  const classes = new Set();
  const ids = new Set();
  const addElement = element => {
    if (!(element instanceof Element)) return;
    if (element.id && ids.size < maxPending) ids.add(element.id);
    if (classes.size < maxPending) {
      for (const name of element.classList) {
        if (classes.size === maxPending) break;
        if (name) classes.add(name);
      }
    }
  };
  const scan = root => {
    addElement(root);
    if (!(root instanceof Element) ||
        (classes.size === maxPending && ids.size === maxPending)) return;
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      addElement(node);
      if (classes.size === maxPending && ids.size === maxPending) break;
    }
  };
  const observer = new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') {
        addElement(record.target);
      } else {
        for (const node of record.addedNodes) scan(node);
      }
      if (classes.size === maxPending && ids.size === maxPending) break;
    }
  });
  const state = {
    observer,
    observing: false,
    start() {
      if (this.observing) return;
      const root = document.documentElement;
      if (!root) return;
      scan(root);
      observer.observe(root, {
        subtree: true,
        childList: true,
        attributes: true,
        attributeFilter: ['class', 'id']
      });
      this.observing = true;
    },
    drain(limit) {
      this.start();
      const take = set => {
        const values = [];
        for (const value of set) {
          values.push(value);
          set.delete(value);
          if (values.length === limit) break;
        }
        return values;
      };
      return JSON.stringify({classes: take(classes), ids: take(ids)});
    }
  };
  state.start();
  globalThis[key] = state;
})();
)JS";

constexpr char kDrainDiscoveryScript[] = R"JS(
(() => {
  const state = globalThis.__seoulCosmeticFilterState;
  return state ? state.drain(256) : '';
})();
)JS";

constexpr char kRemoveDiscoveryScript[] = R"JS(
(() => {
  const key = '__seoulCosmeticFilterState';
  const state = globalThis[key];
  if (state && state.observer) state.observer.disconnect();
  delete globalThis[key];
})();
)JS";

constexpr char kInstallProceduralScriptPrefix[] = R"JS(
(() => {
  const incomingRules = )JS";

constexpr char kInstallProceduralScriptSuffix[] = R"JS(;
  const key = '__seoulProceduralFilterState';
  const prior = globalThis[key];
  if (prior && typeof prior.stop === 'function') prior.stop();
  if (!Array.isArray(incomingRules) || incomingRules.length === 0) {
    delete globalThis[key];
    return;
  }

  const maxCandidatesPerRun = 4096;
  const selectDescendants = (roots, selector, budget) => {
    const output = [];
    const seen = new Set();
    for (const root of roots) {
      if (!root || typeof root.querySelectorAll !== 'function') continue;
      let matches;
      try {
        matches = root.querySelectorAll(selector);
      } catch {
        return [];
      }
      for (const element of matches) {
        if (!(element instanceof Element) || seen.has(element)) continue;
        seen.add(element);
        output.push(element);
        if (--budget.value <= 0) return output;
      }
    }
    return output;
  };
  const applyRule = (rule, budget) => {
    if (!rule || !Array.isArray(rule.selector)) return;
    let candidates = document.documentElement ? [document.documentElement] : [];
    for (const operator of rule.selector) {
      if (!operator || typeof operator.type !== 'string' ||
          typeof operator.arg !== 'string') return;
      if (operator.type === 'css-selector') {
        candidates = selectDescendants(candidates, operator.arg, budget);
      } else if (operator.type === 'has-text') {
        candidates = candidates.filter(
            element => (element.textContent || '').includes(operator.arg));
      } else if (operator.type === 'min-text-length') {
        const minimum = Number(operator.arg);
        candidates = candidates.filter(
            element => (element.textContent || '').length >= minimum);
      } else if (operator.type === 'upward') {
        const levels = Number(operator.arg);
        const parents = [];
        const seen = new Set();
        for (const element of candidates) {
          let parent = element;
          for (let index = 0; index < levels && parent; ++index) {
            parent = parent.parentElement;
          }
          if (parent && !seen.has(parent)) {
            seen.add(parent);
            parents.push(parent);
          }
        }
        candidates = parents;
      } else {
        return;
      }
      if (candidates.length === 0 || budget.value <= 0) return;
    }

    const action = rule.action;
    for (const element of candidates) {
      if (!(element instanceof Element)) continue;
      if (!action) {
        element.style.setProperty('display', 'none', 'important');
      } else if (action.type === 'remove') {
        element.remove();
      } else if (action.type === 'remove-attr') {
        element.removeAttribute(action.arg);
      } else if (action.type === 'remove-class') {
        element.classList.remove(action.arg);
      }
    }
  };

  let timer = 0;
  let running = false;
  const run = () => {
    timer = 0;
    if (running) return;
    running = true;
    const budget = {value: maxCandidatesPerRun};
    try {
      for (const rule of incomingRules) {
        if (budget.value <= 0) break;
        applyRule(rule, budget);
      }
    } finally {
      running = false;
    }
  };
  const schedule = () => {
    if (!timer) timer = setTimeout(run, 50);
  };
  const observer = new MutationObserver(schedule);
  if (document.documentElement) {
    observer.observe(document.documentElement, {
      subtree: true,
      childList: true,
      characterData: true,
      attributes: true,
      attributeFilter: ['class', 'id']
    });
  }
  globalThis[key] = {
    stop() {
      observer.disconnect();
      if (timer) clearTimeout(timer);
      timer = 0;
    }
  };
  run();
})();
)JS";

constexpr char kRemoveProceduralScript[] = R"JS(
(() => {
  const key = '__seoulProceduralFilterState';
  const state = globalThis[key];
  if (state && typeof state.stop === 'function') state.stop();
  delete globalThis[key];
})();
)JS";

void EnsureIsolatedWorldInitialized() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  blink::WebIsolatedWorldInfo info;
  info.human_readable_name = "Project Seoul cosmetic filters";
  info.security_origin =
      blink::WebSecurityOrigin::Create(GURL(kSecurityOrigin));
  info.content_security_policy = blink::WebString::FromUtf8("");
  blink::SetIsolatedWorldInfo(ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS, info);
}

bool IsSafeHideSelector(const std::string& selector) {
  return !selector.empty() && selector.size() <= kMaxSelectorLength &&
         selector.front() != '@' && selector.find('\0') == std::string::npos &&
         selector.find('{') == std::string::npos &&
         selector.find('}') == std::string::npos;
}

std::vector<std::string> ReadStringList(const base::DictValue& dictionary,
                                        std::string_view key) {
  std::vector<std::string> output;
  const base::ListValue* values = dictionary.FindList(key);
  if (!values) {
    return output;
  }
  output.reserve(std::min(values->size(), kMaxIdentifiersPerBatch));
  for (const base::Value& value : *values) {
    const std::string* string = value.GetIfString();
    if (string && !string->empty() && string->size() <= kMaxIdentifierLength) {
      output.push_back(*string);
      if (output.size() == kMaxIdentifiersPerBatch) {
        break;
      }
    }
  }
  return output;
}

std::optional<std::string> SerializeProceduralRules(
    const std::vector<std::string>& default_actions,
    const std::vector<std::string>& additional_actions) {
  base::ListValue rules;
  size_t bytes = 0;
  const auto append = [&](const std::vector<std::string>& actions) {
    for (const std::string& serialized : actions) {
      if (rules.size() == kMaxProceduralActions || serialized.empty() ||
          serialized.size() > kMaxProceduralActionBytes ||
          bytes + serialized.size() > kMaxProceduralPayloadBytes) {
        continue;
      }
      std::optional<base::Value> parsed =
          base::JSONReader::Read(serialized, /*options=*/0, /*max_depth=*/16);
      if (!parsed || !parsed->is_dict()) {
        continue;
      }
      bytes += serialized.size();
      rules.Append(std::move(*parsed));
    }
  };
  append(default_actions);
  append(additional_actions);
  return base::WriteJson(rules);
}

}  // namespace

// static
void CosmeticFilterAgent::Create(content::RenderFrame* render_frame) {
  if (render_frame) {
    new CosmeticFilterAgent(render_frame);
  }
}

CosmeticFilterAgent::CosmeticFilterAgent(content::RenderFrame* render_frame)
    : RenderFrameObserver(render_frame) {
  EnsureIsolatedWorldInitialized();
}

CosmeticFilterAgent::~CosmeticFilterAgent() = default;

void CosmeticFilterAgent::DidCreateNewDocument() {
  ClearForNewDocument();
}

void CosmeticFilterAgent::DidCreateDocumentElement() {
  if (!document_request_started_) {
    BeginForCurrentDocument(/*refresh=*/false);
  }
}

void CosmeticFilterAgent::DidSetPageLifecycleState(
    blink::BFCacheStateChange bfcache_change) {
  if (bfcache_change == blink::BFCacheStateChange::kStoredToBFCache) {
    SuspendForBackForwardCache();
  } else if (bfcache_change ==
             blink::BFCacheStateChange::kRestoredFromBFCache) {
    suspended_ = false;
    RemoveDiscoveryScript();
    document_request_started_ = false;
    BeginForCurrentDocument(/*refresh=*/true);
  }
}

void CosmeticFilterAgent::OnDestruct() {
  delete this;
}

void CosmeticFilterAgent::ClearForNewDocument() {
  ++generation_;
  suspended_ = false;
  document_request_started_ = false;
  request_in_flight_ = false;
  poll_timer_.Stop();
  host_.reset();
  discovery_installed_ = false;
  procedural_rules_installed_ = false;
  style_sheet_inserted_ = false;
  query_generics_ = false;
  selectors_.clear();
  executed_isolated_scripts_.clear();
  style_sheet_.clear();
  style_sheet_bytes_ = 0;
}

void CosmeticFilterAgent::SuspendForBackForwardCache() {
  RemoveProceduralRules();
  ++generation_;
  suspended_ = true;
  request_in_flight_ = false;
  poll_timer_.Stop();
  host_.reset();
}

void CosmeticFilterAgent::BeginForCurrentDocument(bool refresh) {
  if (suspended_ || !render_frame() || !render_frame()->GetWebFrame()) {
    return;
  }
  blink::WebDocument document = render_frame()->GetWebFrame()->GetDocument();
  if (document.IsNull()) {
    return;
  }
  document_request_started_ = true;
  const uint64_t generation = ++generation_;
  if (document.IsPrerendering()) {
    document.AddPostPrerenderingActivationStep(
        base::BindOnce(&CosmeticFilterAgent::RequestResources,
                       weak_factory_.GetWeakPtr(), generation, refresh));
    return;
  }
  RequestResources(generation, refresh);
}

void CosmeticFilterAgent::RequestResources(uint64_t generation, bool refresh) {
  if (generation != generation_ || suspended_ || !render_frame()) {
    return;
  }
  if (!host_.is_bound()) {
    render_frame()->GetBrowserInterfaceBroker().GetInterface(
        host_.BindNewPipeAndPassReceiver());
  }
  host_->GetCosmeticResources(
      base::BindOnce(&CosmeticFilterAgent::OnGotResources,
                     weak_factory_.GetWeakPtr(), generation, refresh));
}

void CosmeticFilterAgent::OnGotResources(
    uint64_t generation,
    bool /*refresh*/,
    adblock::mojom::CosmeticResourcesPtr resources) {
  if (generation != generation_ || suspended_ || !resources) {
    return;
  }
  request_in_flight_ = false;
  if (!resources->enabled || !resources->default_rules ||
      !resources->additional_rules) {
    RemoveDiscoveryScript();
    RemoveProceduralRules();
    ReplaceSelectors({}, {});
    query_generics_ = false;
    poll_timer_.Stop();
    return;
  }

  ReplaceSelectors(resources->default_rules->selectors,
                   resources->additional_rules->selectors);
  ExecuteIsolatedScript(resources->default_rules->isolated_script);
  ExecuteIsolatedScript(resources->additional_rules->isolated_script);
  InstallProceduralRules(resources->default_rules->procedural_actions,
                         resources->additional_rules->procedural_actions);
  query_generics_ = resources->default_rules->query_generics ||
                    resources->additional_rules->query_generics;
  if (!query_generics_) {
    RemoveDiscoveryScript();
    poll_timer_.Stop();
    return;
  }
  InstallDiscoveryScript();
  PollIdentifiers();
  if (!poll_timer_.IsRunning()) {
    poll_timer_.Start(FROM_HERE, kPollInterval, this,
                      &CosmeticFilterAgent::PollIdentifiers);
  }
}

void CosmeticFilterAgent::InstallDiscoveryScript() {
  if (discovery_installed_ || !render_frame() ||
      !render_frame()->GetWebFrame()) {
    return;
  }
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
      blink::WebScriptSource(
          blink::WebString::FromUtf8(kInstallDiscoveryScript)),
      blink::BackForwardCacheAware::kAllow);
  discovery_installed_ = true;
}

void CosmeticFilterAgent::RemoveDiscoveryScript() {
  if (!discovery_installed_) {
    return;
  }
  if (render_frame() && render_frame()->GetWebFrame()) {
    render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
        ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
        blink::WebScriptSource(
            blink::WebString::FromUtf8(kRemoveDiscoveryScript)),
        blink::BackForwardCacheAware::kAllow);
  }
  discovery_installed_ = false;
}

void CosmeticFilterAgent::PollIdentifiers() {
  if (suspended_ || request_in_flight_ || !query_generics_ ||
      !host_.is_bound() || !render_frame() || !render_frame()->GetWebFrame()) {
    return;
  }
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Value> result =
      frame->ExecuteScriptInIsolatedWorldAndReturnValue(
          ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
          blink::WebScriptSource(
              blink::WebString::FromUtf8(kDrainDiscoveryScript)),
          blink::BackForwardCacheAware::kAllow);
  if (result.IsEmpty()) {
    return;
  }
  std::string json;
  if (!gin::Converter<std::string>::FromV8(isolate, result, &json) ||
      json.empty()) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  std::vector<std::string> classes =
      ReadStringList(parsed->GetDict(), "classes");
  std::vector<std::string> ids = ReadStringList(parsed->GetDict(), "ids");
  if (classes.empty() && ids.empty()) {
    return;
  }

  request_in_flight_ = true;
  const uint64_t generation = generation_;
  host_->GetDynamicCosmeticSelectors(
      std::move(classes), std::move(ids),
      base::BindOnce(&CosmeticFilterAgent::OnGotDynamicSelectors,
                     weak_factory_.GetWeakPtr(), generation));
}

void CosmeticFilterAgent::OnGotDynamicSelectors(
    uint64_t generation,
    adblock::mojom::DynamicCosmeticSelectorsPtr selectors) {
  if (generation != generation_ || suspended_) {
    return;
  }
  request_in_flight_ = false;
  if (!selectors) {
    return;
  }
  bool changed = AppendSelectors(selectors->default_selectors);
  changed = AppendSelectors(selectors->additional_selectors) || changed;
  if (changed) {
    ApplyStyleSheet();
  }
}

void CosmeticFilterAgent::ReplaceSelectors(
    const std::vector<std::string>& default_selectors,
    const std::vector<std::string>& additional_selectors) {
  selectors_.clear();
  style_sheet_.clear();
  style_sheet_bytes_ = 0;
  AppendSelectors(default_selectors);
  AppendSelectors(additional_selectors);
  ApplyStyleSheet();
}

void CosmeticFilterAgent::ExecuteIsolatedScript(const std::string& script) {
  if (script.empty() || script.size() > kMaxIsolatedScriptBytes ||
      script.find('\0') != std::string::npos ||
      executed_isolated_scripts_.contains(script) || !render_frame() ||
      !render_frame()->GetWebFrame()) {
    return;
  }
  const std::string wrapped = "(() => {\n'use strict';\n" + script + "\n})();";
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
      blink::WebScriptSource(blink::WebString::FromUtf8(wrapped)),
      blink::BackForwardCacheAware::kAllow);
  executed_isolated_scripts_.insert(script);
}

void CosmeticFilterAgent::InstallProceduralRules(
    const std::vector<std::string>& default_actions,
    const std::vector<std::string>& additional_actions) {
  if (!render_frame() || !render_frame()->GetWebFrame()) {
    return;
  }
  const std::optional<std::string> serialized =
      SerializeProceduralRules(default_actions, additional_actions);
  if (!serialized || *serialized == "[]") {
    RemoveProceduralRules();
    return;
  }
  const std::string script = std::string(kInstallProceduralScriptPrefix) +
                             *serialized + kInstallProceduralScriptSuffix;
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
      blink::WebScriptSource(blink::WebString::FromUtf8(script)),
      blink::BackForwardCacheAware::kAllow);
  procedural_rules_installed_ = true;
}

void CosmeticFilterAgent::RemoveProceduralRules() {
  if (!procedural_rules_installed_) {
    return;
  }
  if (render_frame() && render_frame()->GetWebFrame()) {
    render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
        ISOLATED_WORLD_ID_SEOUL_COSMETIC_FILTERS,
        blink::WebScriptSource(
            blink::WebString::FromUtf8(kRemoveProceduralScript)),
        blink::BackForwardCacheAware::kAllow);
  }
  procedural_rules_installed_ = false;
}

bool CosmeticFilterAgent::AppendSelectors(
    const std::vector<std::string>& selectors) {
  bool changed = false;
  for (const std::string& selector : selectors) {
    if (selectors_.size() == kMaxSelectors || !IsSafeHideSelector(selector) ||
        selectors_.contains(selector)) {
      continue;
    }
    const std::string rule = selector + "{display:none!important;}\n";
    if (style_sheet_bytes_ + rule.size() > kMaxStyleSheetBytes) {
      break;
    }
    selectors_.insert(selector);
    style_sheet_.append(rule);
    style_sheet_bytes_ += rule.size();
    changed = true;
  }
  return changed;
}

void CosmeticFilterAgent::ApplyStyleSheet() {
  if (!render_frame() || !render_frame()->GetWebFrame()) {
    return;
  }
  blink::WebDocument document = render_frame()->GetWebFrame()->GetDocument();
  if (document.IsNull()) {
    return;
  }
  if (style_sheet_inserted_) {
    document.RemoveInsertedStyleSheet(style_sheet_key_,
                                      blink::WebCssOrigin::kUser);
    style_sheet_inserted_ = false;
  }
  if (style_sheet_.empty()) {
    return;
  }
  style_sheet_key_ = document.InsertStyleSheet(
      blink::WebString::FromUtf8(style_sheet_), nullptr,
      blink::WebCssOrigin::kUser, blink::BackForwardCacheAware::kAllow);
  style_sheet_inserted_ = true;
}

void CosmeticFilterAgent::RemoveStyleSheet() {
  if (!style_sheet_inserted_ || !render_frame() ||
      !render_frame()->GetWebFrame()) {
    style_sheet_inserted_ = false;
    return;
  }
  blink::WebDocument document = render_frame()->GetWebFrame()->GetDocument();
  if (!document.IsNull()) {
    document.RemoveInsertedStyleSheet(style_sheet_key_,
                                      blink::WebCssOrigin::kUser);
  }
  style_sheet_inserted_ = false;
}

}  // namespace seoul::renderer
