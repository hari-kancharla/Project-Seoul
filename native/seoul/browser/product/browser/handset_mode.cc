// Project Seoul Handset - live WebContents application.

#include "seoul/browser/product/browser/handset_mode.h"

#include "base/memory/raw_ptr.h"
#include "base/version_info/version_info.h"
#include "components/embedder_support/user_agent_utils.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/seoul_device_presentation.h"
#include "third_party/blink/public/common/widget/device_emulation_params.h"
#include "ui/display/mojom/screen_orientation.mojom-shared.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "seoul/browser/handset/user_agent_profile.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/handset/viewport_math.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/webpreferences/web_preferences.mojom.h"

namespace seoul {

namespace {

// The emulation parameters for a resolved device.
//
// screen_size and view_size are both set, and deliberately: the screen is what
// window.screen and any DPR-driven asset choice reads, the view is what the
// page is laid out in. Leaving either empty makes blink substitute the real
// display, which is how a page ends up laid out at phone width while still
// reporting a 27-inch screen.
blink::DeviceEmulationParams EmulationParamsFor(const HandsetMetrics& metrics) {
  blink::DeviceEmulationParams params;
  params.screen_type = blink::mojom::EmulatedScreenType::kMobile;
  params.screen_size =
      gfx::Size(metrics.screen_width_dip, metrics.screen_height_dip);
  params.view_size = gfx::Size(metrics.view_width_dip, metrics.view_height_dip);
  params.device_scale_factor = metrics.device_scale_factor;
  params.screen_orientation_type =
      metrics.orientation == HandsetOrientation::kLandscape
          ? display::mojom::ScreenOrientation::kLandscapePrimary
          : display::mojom::ScreenOrientation::kPortraitPrimary;
  params.screen_orientation_angle = metrics.orientation_angle;
  return params;
}

class HandsetModeState : public content::WebContentsUserData<HandsetModeState>,
                         public content::WebContentsObserver {
 public:
  HandsetModeState(const HandsetModeState&) = delete;
  HandsetModeState& operator=(const HandsetModeState&) = delete;
  ~HandsetModeState() override = default;

  const HandsetProfile* profile() const { return profile_; }
  HandsetOrientation orientation() const { return orientation_; }
  HandsetSnapMode snap_mode() const { return snap_mode_; }

  void Set(const HandsetProfile* profile,
           HandsetOrientation orientation,
           HandsetSnapMode snap_mode,
           int view_width_dip,
           int view_height_dip) {
    profile_ = profile;
    orientation_ = orientation;
    snap_mode_ = snap_mode;
    view_width_dip_ = view_width_dip;
    view_height_dip_ = view_height_dip;
  }

  // The User-Agent state the tab had before Handset mode took it over. A tab
  // can already carry an override - a restored session replays a saved one,
  // and Chromium's own Request Desktop/Tablet Site sets one - so turning
  // Handset off has to put back what was there rather than clearing to the
  // browser default, which would silently revoke an unrelated setting.
  bool has_restore_point() const { return has_restore_point_; }
  const blink::UserAgentOverride& restore_override() const {
    return restore_override_;
  }
  bool restore_entry_overriding() const { return restore_entry_overriding_; }

  void CaptureRestorePoint(blink::UserAgentOverride override,
                           bool entry_overriding) {
    if (has_restore_point_) {
      return;
    }
    restore_override_ = std::move(override);
    restore_entry_overriding_ = entry_overriding;
    has_restore_point_ = true;
  }
  void ClearRestorePoint() {
    restore_override_ = blink::UserAgentOverride();
    restore_entry_overriding_ = false;
    has_restore_point_ = false;
  }

 private:
  friend class content::WebContentsUserData<HandsetModeState>;

  explicit HandsetModeState(content::WebContents* web_contents)
      : content::WebContentsUserData<HandsetModeState>(*web_contents),
        content::WebContentsObserver(web_contents) {}

  // A committed navigation is where a Handset would quietly stop being one.
  // The mobile preferences survive it, because Chromium recomputes them and
  // Seoul contributes to that recomputation - but device emulation is state on
  // a widget, and a cross-process navigation builds a new one. Re-applying it
  // here is what makes this a mode the page keeps rather than a setting that
  // lasts until the first link.
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (!profile_ || !handle || !handle->IsInPrimaryMainFrame() ||
        !handle->HasCommitted() || handle->IsSameDocument()) {
      return;
    }
    // kKeepCache: the device did not change, this is a re-sync onto a new
    // widget, and clearing the memory cache for it would throw away exactly
    // the resources the new document is about to use.
    content::SetDevicePresentation(
        web_contents(),
        EmulationParamsFor(ResolveHandsetMetrics(*profile_, orientation_,
                                                 snap_mode_, view_width_dip_,
                                                 view_height_dip_)),
        /*preserve_cache=*/true);
    content::SetTouchPresentationEnabled(web_contents(), true);
  }

  // Points into the process-wide immutable catalogue, which outlives every
  // WebContents, so this is a borrow rather than a copy of the device.
  raw_ptr<const HandsetProfile> profile_ = nullptr;
  HandsetOrientation orientation_ = HandsetOrientation::kPortrait;
  HandsetSnapMode snap_mode_ = HandsetSnapMode::kSnapToProfile;
  blink::UserAgentOverride restore_override_;
  bool restore_entry_overriding_ = false;
  bool has_restore_point_ = false;
  // The window geometry the presentation was last resolved against, so a
  // re-sync after navigation reproduces the same device rather than falling
  // back to the profile's own size and silently resizing the page.
  int view_width_dip_ = 0;
  int view_height_dip_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(HandsetModeState);

HandsetModeState* StateFor(content::WebContents* web_contents) {
  return web_contents ? HandsetModeState::FromWebContents(web_contents)
                      : nullptr;
}

// Builds the override carrying both signals at once. Starting from the
// browser's own metadata keeps the brand list and its GREASE entry intact;
// only the fields that describe the device are replaced, so the hints stay a
// well-formed Chromium hint set that happens to describe a phone.
blink::UserAgentOverride BuildOverride(const HandsetProfile& profile) {
  const HandsetUserAgent handset_agent =
      BuildHandsetUserAgent(profile, version_info::GetMajorVersionNumber());

  blink::UserAgentOverride override;
  override.ua_string_override = handset_agent.ua_string;
  if (!handset_agent.send_client_hints) {
    // Left unset on purpose: blink reads an absent metadata override next to a
    // present string override as "send no UA client-hint request headers".
    // That is the header behavior of a real iOS device. It does not reach
    // navigator.userAgentData, which survives the suppression - see the gap
    // recorded in seoul/browser/handset/user_agent_profile.h.
    return override;
  }

  blink::UserAgentMetadata metadata = embedder_support::GetUserAgentMetadata();
  metadata.platform = handset_agent.platform;
  metadata.platform_version = handset_agent.platform_version;
  metadata.model = handset_agent.model;
  metadata.architecture = handset_agent.architecture;
  metadata.mobile = handset_agent.mobile;
  metadata.form_factors = handset_agent.form_factors;
  // A phone reports no CPU bitness or WOW64 state; leaving the desktop values
  // in place would contradict the architecture hint next to them.
  metadata.bitness = "";
  metadata.wow64 = false;
  override.ua_metadata_override = std::move(metadata);
  return override;
}

// Assigns and reports whether the value actually moved, so the caller can tell
// an after-navigation recomputation that something changed.
template <typename T>
bool AssignChanged(T* field, T value) {
  if (*field == value) {
    return false;
  }
  *field = value;
  return true;
}

}  // namespace

bool CanHandsetWebContents(content::WebContents* web_contents) {
  // Reuses the Boost eligibility answer for the window half - ordinary,
  // non-incognito, live browser window - so the two site controls appear and
  // disappear together instead of drifting apart.
  return EligibleBrowserFor(web_contents) &&
         web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

bool ToggleHandsetForWebContents(content::WebContents* web_contents) {
  if (!CanHandsetWebContents(web_contents)) {
    return false;
  }
  if (IsHandsetModeEnabled(web_contents)) {
    DisableHandsetMode(web_contents);
    return false;
  }
  // The device the user has expressed no preference about. Orientation and
  // snap are the defaults a phone arrives in.
  return EnableHandsetMode(web_contents, DefaultHandsetProfile().id,
                           HandsetOrientation::kPortrait,
                           HandsetSnapMode::kSnapToProfile);
}

bool EnableHandsetMode(content::WebContents* web_contents,
                       const std::string& profile_id,
                       HandsetOrientation orientation,
                       HandsetSnapMode snap_mode,
                       int free_width_dip,
                       int free_height_dip) {
  if (!web_contents) {
    return false;
  }
  const HandsetProfile* profile = FindHandsetProfile(profile_id);
  if (!profile) {
    return false;
  }

  HandsetModeState::CreateForWebContents(web_contents);
  HandsetModeState* state = StateFor(web_contents);
  if (!state) {
    return false;
  }

  content::NavigationController& controller = web_contents->GetController();
  content::NavigationEntry* entry = controller.GetLastCommittedEntry();
  state->CaptureRestorePoint(web_contents->GetUserAgentOverride(),
                             entry && entry->GetIsOverridingUserAgent());
  const bool changing_device = state->profile() != profile;
  const HandsetMetrics metrics = ResolveHandsetMetrics(
      *profile, orientation, snap_mode, free_width_dip, free_height_dip);
  state->Set(profile, orientation, snap_mode, metrics.view_width_dip,
             metrics.view_height_dip);

  // The committed entry has to be flagged before the override is installed.
  // An override that no entry claims is inert: the renderer asks the entry
  // whether this navigation overrides the User-Agent, so without the flag the
  // stored string is never consulted and the page keeps its desktop identity.
  // This is the same order Chromium's own Request Desktop Site uses.
  if (entry) {
    entry->SetIsOverridingUserAgent(true);
  }
  // `override_in_new_tabs` is false, matching Chromium's own site-override
  // commands. It does not propagate to popups - a new WebContents does not
  // inherit the flag - so passing true would buy nothing and would overstate
  // what happens to a window the page opens. Popup inheritance, if it is
  // wanted, has to be done by putting the opened contents into Handset mode
  // explicitly.
  web_contents->SetUserAgentOverride(BuildOverride(*profile),
                                     /*override_in_new_tabs=*/false);

  // Preferences first, emulation second, and the order is load-bearing rather
  // than tidy. Blink's emulator snapshots the embedder's viewport settings to
  // restore later, and once mobile emulation is on it swallows further
  // preference pushes. Enabling emulation before the mobile preferences have
  // landed captures the desktop defaults as the baseline, and turning the mode
  // off then restores a page to a layout it never had.
  web_contents->OnWebPreferencesChanged();

  // kClearCache: the device genuinely changed, so resources chosen for the old
  // one - a desktop-width image set - should not be reused for this one.
  content::SetDevicePresentation(web_contents, EmulationParamsFor(metrics),
                                 /*preserve_cache=*/false);
  // Real touch delivery. Without it a page can be phone-shaped and still not
  // respond to a swipe, because nothing dispatches touchstart at all.
  content::SetTouchPresentationEnabled(web_contents, true);

  // The page in front of the user was fetched and laid out as a desktop page.
  // Nothing above re-runs that, so without a reload the mode is only visible
  // on the next navigation. Reload the original request rather than the
  // current URL so a redirect chain is re-followed under the mobile identity,
  // which is how sites that redirect to a mobile host are reached at all.
  if (changing_device) {
    controller.LoadOriginalRequestURL();
  }
  return true;
}

void DisableHandsetMode(content::WebContents* web_contents) {
  HandsetModeState* state = StateFor(web_contents);
  if (!state || !state->profile()) {
    return;
  }
  // Emulation off before the preferences are recomputed, mirroring enable.
  // While emulation is on the emulator swallows preference pushes; taking it
  // down first means the recomputation below is the authoritative one and the
  // page ends up with the desktop layout it actually had.
  content::SetTouchPresentationEnabled(web_contents, false);
  content::ClearDevicePresentation(web_contents);

  state->Set(nullptr, HandsetOrientation::kPortrait,
             HandsetSnapMode::kSnapToProfile, 0, 0);

  content::NavigationController& controller = web_contents->GetController();
  if (content::NavigationEntry* entry = controller.GetLastCommittedEntry()) {
    entry->SetIsOverridingUserAgent(state->restore_entry_overriding());
  }
  // Restore what the tab had, which is not necessarily nothing.
  web_contents->SetUserAgentOverride(state->restore_override(),
                                     /*override_in_new_tabs=*/false);
  state->ClearRestorePoint();

  web_contents->OnWebPreferencesChanged();
  controller.LoadOriginalRequestURL();
}

bool IsHandsetModeEnabled(content::WebContents* web_contents) {
  const HandsetModeState* state = StateFor(web_contents);
  return state && state->profile();
}

const HandsetProfile* HandsetProfileFor(content::WebContents* web_contents) {
  const HandsetModeState* state = StateFor(web_contents);
  return state ? state->profile() : nullptr;
}

HandsetMetrics HandsetMetricsFor(content::WebContents* web_contents,
                                 int free_width_dip,
                                 int free_height_dip) {
  const HandsetModeState* state = StateFor(web_contents);
  const HandsetProfile* profile = state ? state->profile() : nullptr;
  if (!profile) {
    // Handset mode is off, so there is no live device to report. Resolving the
    // default profile keeps the return type total and gives the window layer
    // the size it would open at.
    return ResolveHandsetMetrics(DefaultHandsetProfile(),
                                 HandsetOrientation::kPortrait,
                                 HandsetSnapMode::kSnapToProfile,
                                 free_width_dip, free_height_dip);
  }
  return ResolveHandsetMetrics(*profile, state->orientation(),
                               state->snap_mode(), free_width_dip,
                               free_height_dip);
}

bool HandsetRendererIsShared(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  content::RenderFrameHost* const main_frame =
      web_contents->GetPrimaryMainFrame();
  content::RenderProcessHost* const process =
      main_frame ? main_frame->GetProcess() : nullptr;
  if (!process) {
    return false;
  }
  bool foreign = false;
  process->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    // Frames of this same page are not foreign however deeply nested, so the
    // comparison is against the contents that own the frame rather than the
    // frame's immediate parent.
    if (!foreign &&
        content::WebContents::FromRenderFrameHost(frame) != web_contents) {
      foreign = true;
    }
  });
  return foreign;
}

bool OverrideHandsetWebPreferences(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* web_preferences) {
  if (!web_preferences || !IsHandsetModeEnabled(web_contents)) {
    return false;
  }

  bool changed = false;

  // The mobile layout path. These have Android/iOS-only compile-time defaults,
  // but nothing about them is platform-specific at runtime: turning them on is
  // what makes Blink honor <meta name="viewport"> and lay the page out at the
  // emulated width instead of treating it as a desktop window.
  changed |= AssignChanged(&web_preferences->viewport_enabled, true);
  changed |= AssignChanged(&web_preferences->viewport_meta_enabled, true);
  changed |= AssignChanged(&web_preferences->viewport_style,
                           blink::mojom::ViewportStyle::kMobile);
  changed |=
      AssignChanged(&web_preferences->shrinks_viewport_contents_to_fit, true);
  changed |= AssignChanged(
      &web_preferences->default_minimum_page_scale_factor, 0.25f);
  changed |= AssignChanged(
      &web_preferences->default_maximum_page_scale_factor, 5.0f);
  changed |= AssignChanged(
      &web_preferences->auto_zoom_focused_editable_to_legible_scale, true);

  // Blink's mobile emulator turns this on, but the setter it ends up in
  // bypasses the emulator's own bookkeeping, so every later preference
  // recomputation would write the macOS default back over it and silently drop
  // -webkit-text-size-adjust. Setting it here makes the preference agree with
  // what emulation wants, so it survives recomputation either way.
  changed |= AssignChanged(&web_preferences->text_size_adjust_enabled, true);
  // A Handset window resize is a device rotation, not a desktop window resize.
  // Without this the page gets neither the orientation change event nor the
  // relayout a rotating phone would produce.
  changed |= AssignChanged(
      &web_preferences->main_frame_resizes_are_orientation_changes, true);

  // Touch has to be declared here, not left to the touch emulator. The
  // emulator routes events; what makes touch *visible* to the page -
  // `ontouchstart`, `navigator.maxTouchPoints`, and every library that
  // feature-detects on them - is these two preferences. Without them a page
  // presented as a phone reports a device with no touch screen, and a script
  // that branches on touch support takes the desktop branch.
  changed |= AssignChanged(
      &web_preferences->touch_event_feature_detection_enabled, true);
  // Five is what iOS reports and is the common Android value; the exact count
  // matters far less than being non-zero, which is the test every real site
  // applies.
  changed |= AssignChanged(&web_preferences->pointer_events_max_touch_points, 5);

  // Coarse pointer, no hover. A site whose navigation opens on hover is
  // unusable without this, and no viewport width substitutes for it.
  changed |= AssignChanged(
      &web_preferences->available_pointer_types,
      static_cast<int>(blink::mojom::PointerType::kPointerCoarseType));
  changed |= AssignChanged(&web_preferences->primary_pointer_type,
                           blink::mojom::PointerType::kPointerCoarseType);
  changed |= AssignChanged(
      &web_preferences->available_hover_types,
      static_cast<int>(blink::mojom::HoverType::kHoverNone));
  changed |= AssignChanged(&web_preferences->primary_hover_type,
                           blink::mojom::HoverType::kHoverNone);
  return changed;
}

}  // namespace seoul
