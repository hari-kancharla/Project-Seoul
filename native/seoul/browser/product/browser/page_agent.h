// Project Seoul product runtime - the browser-owned page agent.
// The model never supplies executable JavaScript. This fixed, reviewed agent
// observes a tab through the accessibility tree and performs typed actions
// through AXActionData. Element handles are opaque (window/tab/generation/
// node); they expire on navigation, document replacement, or a new
// observation generation, so a stale handle can never act on a fresh
// document.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_PAGE_AGENT_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_PAGE_AGENT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/page_field_safety.h"
#include "seoul/browser/semantic/semantic_types.h"

namespace content {
class ScopedAccessibilityMode;
class WebContents;
}

namespace seoul {

class PageAgentTabObserver;

// A bounded semantic observation of one document: identity plus visible
// accessible elements. No raw HTML, no coordinates.
struct PageObservation {
  PageObservation();
  PageObservation(const PageObservation&);
  PageObservation(PageObservation&&);
  PageObservation& operator=(const PageObservation&);
  PageObservation& operator=(PageObservation&&);
  ~PageObservation();

  LiveTabKey tab;
  uint64_t generation = 0;  // increments per observation of this tab
  std::string url;          // origin + path, no fragments/credentials
  std::string title;
  bool loaded = false;

  struct Element {
    Element();
    Element(const Element&);
    Element(Element&&);
    Element& operator=(const Element&);
    Element& operator=(Element&&);
    ~Element();

    std::string handle;  // opaque, generation-scoped
    std::string role;    // accessible role (button, textbox, link, ...)
    std::string name;    // accessible name
    bool enabled = true;
    bool focusable = false;
    bool editable = false;
    bool agent_writable = false;
    PageFieldSensitivity sensitivity = PageFieldSensitivity::kNone;
  };
  std::vector<Element> elements;
};

// A typed page action. No arbitrary script; every action maps to an
// AXActionData against a live node resolved from a non-expired handle.
enum class PageActionKind {
  kClick,
  kType,
  kClear,
  kSelectOption,
  kToggle,
  kScrollToElement,
  kFocus,
};

struct PageActionRequest {
  PageActionRequest();
  PageActionRequest(const PageActionRequest&);
  PageActionRequest(PageActionRequest&&);
  PageActionRequest& operator=(const PageActionRequest&);
  PageActionRequest& operator=(PageActionRequest&&);
  ~PageActionRequest();

  PageActionKind kind = PageActionKind::kClick;
  std::string handle;  // element handle from the current observation
  std::string value;   // kType / kSelectOption text
};

enum class PageActionStatus {
  kOk,
  kUnknownTab,
  kExpiredHandle,  // navigation/replacement/new generation since capture
  kElementGone,    // handle valid but the node no longer exists
  kNotActionable,  // element not enabled/editable for this action
  kSensitiveField, // password/payment/one-time-code requires user/autofill
  kActionFailed,
};

// Resolves a Seoul tab key to its live WebContents. Injected so the agent
// stays independent of the tab strip wiring.
using WebContentsResolver =
    base::RepeatingCallback<content::WebContents*(const LiveTabKey&)>;
using PageActionVerificationCallback =
    base::OnceCallback<void(PageActionStatus status)>;

class PageAgent {
 public:
  explicit PageAgent(WebContentsResolver resolver);
  PageAgent(const PageAgent&) = delete;
  PageAgent& operator=(const PageAgent&) = delete;
  ~PageAgent();

  // Captures a fresh observation of `tab`, bumping its generation and
  // expiring all prior handles for that tab. Asynchronous (the AX snapshot is
  // async); the callback carries nullopt when the tab is not resolvable.
  void Observe(
      const LiveTabKey& tab,
      base::OnceCallback<void(std::optional<PageObservation>)> callback);

  // Performs a typed action. Revalidates window/tab/generation/handle before
  // touching the page; a stale handle is rejected, never coerced.
  PageActionStatus PerformAction(const LiveTabKey& tab,
                                 const PageActionRequest& request);
  // Dispatches one validated AX action and resolves only after a fresh AX
  // snapshot observes a document/focus/value change (or a committed
  // navigation). A dispatch with no observable postcondition fails.
  void PerformActionAndVerify(const LiveTabKey& tab,
                              const PageActionRequest& request,
                              PageActionVerificationCallback callback);

  // Invalidates all handles for a tab (called by the runtime on navigation or
  // document replacement so the next action re-observes).
  void InvalidateTab(const LiveTabKey& tab);

 private:
  struct NodeBinding {
    int32_t ax_node_id = 0;
    std::string role;
    bool editable = false;
    bool enabled = true;
    PageFieldSensitivity sensitivity = PageFieldSensitivity::kNone;
  };
  struct TabGeneration {
    TabGeneration();
    TabGeneration(const TabGeneration&);
    TabGeneration(TabGeneration&&);
    TabGeneration& operator=(const TabGeneration&);
    TabGeneration& operator=(TabGeneration&&);
    ~TabGeneration();

    uint64_t generation = 0;
    uint64_t document_revision = 0;
    uint64_t document_fingerprint = 0;
    std::map<std::string, NodeBinding> handles;  // handle -> node
  };

  void OnSnapshot(
      LiveTabKey tab,
      uint64_t expected_generation,
      base::OnceCallback<void(std::optional<PageObservation>)> callback,
      /* ui::AXTreeUpdate */ void* update_ptr);
  void RequestActionVerification(
      LiveTabKey tab,
      uint64_t expected_document_revision,
      uint64_t before_fingerprint,
      int attempt,
      PageActionVerificationCallback callback);
  void OnActionVerificationSnapshot(
      LiveTabKey tab,
      uint64_t expected_document_revision,
      uint64_t before_fingerprint,
      int attempt,
      PageActionVerificationCallback callback,
      /* ui::AXTreeUpdate */ void* update_ptr);
  void ExpireHandles(LiveTabKey tab, uint64_t expected_generation);

  WebContentsResolver resolver_;
  std::map<LiveTabKey, TabGeneration> tabs_;
  // A live scoped mode keeps renderer accessibility available between the
  // one-shot observation and the later handle action.
  std::map<LiveTabKey, std::unique_ptr<content::ScopedAccessibilityMode>>
      accessibility_modes_;
  std::map<LiveTabKey, std::unique_ptr<PageAgentTabObserver>>
      navigation_observers_;
  base::WeakPtrFactory<PageAgent> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_PAGE_AGENT_H_
