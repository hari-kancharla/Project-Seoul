// Project Seoul Handset.
//
// A Handset is one web page presented as a phone: exact device metrics, a
// mobile viewport, coarse-pointer media features, a coherent mobile User-Agent
// with matching client hints, and injected touch instead of a mouse. It is a
// browser-owned mode on a WebContents, not a DevTools session and not a
// resized window, so it survives navigation, process swaps, and profile
// changes the way any other product state does.
//
// This header is the pure model: device descriptions and the derived metrics.
// It depends on //base only, so the geometry and User-Agent rules are unit
// tested without a browser.

#ifndef SEOUL_BROWSER_HANDSET_HANDSET_TYPES_H_
#define SEOUL_BROWSER_HANDSET_HANDSET_TYPES_H_

#include <string>
#include <vector>

namespace seoul {

inline constexpr int kHandsetSchemaVersion = 1;

// A Handset window is chrome-less, so the page is the whole window. These
// bounds keep a caller from asking for a "phone" that is neither.
inline constexpr int kMinHandsetWidthDip = 240;
inline constexpr int kMaxHandsetWidthDip = 1366;
inline constexpr int kMinHandsetHeightDip = 320;
inline constexpr int kMaxHandsetHeightDip = 1600;

// Device pixel ratio bounds. Below 1 nothing renders sharply; above 4 the
// raster cost is real and no shipping phone exceeds it.
inline constexpr float kMinHandsetScaleFactor = 1.0f;
inline constexpr float kMaxHandsetScaleFactor = 4.0f;

// Which platform a device profile emulates. This selects the User-Agent
// family, the client-hint platform, and the overlay-scrollbar behavior; it is
// not a branch on any particular site.
enum class HandsetPlatform {
  kIOS,
  kAndroid,
};

// Phone or tablet. This changes the User-Agent shape (a tablet does not carry
// Android's "Mobile" token, and iPadOS identifies itself as iPad) and the
// Sec-CH-UA-Form-Factors hint. It is not a second size axis: the size is
// already the profile's.
enum class HandsetFormFactor {
  kPhone,
  kTablet,
};

enum class HandsetOrientation {
  kPortrait,
  kLandscape,
};

// How a freely resized Handset window resolves to device metrics.
enum class HandsetSnapMode {
  // The window is exactly the chosen profile's portrait or landscape size and
  // resists free resizing. This is the option the product exposes as "snap to
  // device".
  kSnapToProfile,
  // The window resizes freely; the emulated viewport follows the window and
  // the nearest profile supplies the scale factor and User-Agent family.
  kFree,
};

// One emulated device. Sizes are CSS pixels (density-independent), matching
// blink::DeviceEmulationParams, which documents its sizes the same way.
struct HandsetProfile {
  HandsetProfile();
  HandsetProfile(std::string id,
                 std::string label,
                 HandsetPlatform platform,
                 HandsetFormFactor form_factor,
                 int portrait_width_dip,
                 int portrait_height_dip,
                 float device_scale_factor,
                 std::string platform_version,
                 std::string model);
  HandsetProfile(const HandsetProfile&);
  HandsetProfile& operator=(const HandsetProfile&);
  ~HandsetProfile();

  // Stable identifier persisted in profile prefs and used on the wire. Never
  // localized and never reused for a different device.
  std::string id;
  // Human label for the picker.
  std::string label;
  HandsetPlatform platform = HandsetPlatform::kIOS;
  HandsetFormFactor form_factor = HandsetFormFactor::kPhone;
  int portrait_width_dip = 0;
  int portrait_height_dip = 0;
  float device_scale_factor = 1.0f;
  // Operating-system version reported in the User-Agent and in the
  // Sec-CH-UA-Platform-Version client hint. Format is the platform's own:
  // dotted for iOS, a major number for Android.
  std::string platform_version;
  // Device model reported in the Sec-CH-UA-Model client hint. Empty for iOS,
  // which does not report a model hint.
  std::string model;
};

// The fully resolved geometry for a live Handset. Everything the integration
// layer needs to program the widget, derived once so the browser layer holds
// no geometry rules of its own.
struct HandsetMetrics {
  int view_width_dip = 0;
  int view_height_dip = 0;
  int screen_width_dip = 0;
  int screen_height_dip = 0;
  float device_scale_factor = 1.0f;
  HandsetOrientation orientation = HandsetOrientation::kPortrait;
  // Degrees reported to the Screen Orientation API: 0 and 90 here, matching
  // the two orientations this mode supports.
  int orientation_angle = 0;
};

// Returns the profiles this build can emulate, in picker order. The list is
// immutable and shared; it holds no mutable product state.
const std::vector<HandsetProfile>& HandsetProfiles();

// Looks up a profile by id. Returns nullptr when the id is unknown, so a
// stale persisted id fails closed instead of silently selecting a default.
const HandsetProfile* FindHandsetProfile(const std::string& id);

// The profile a Handset opens with when the user has expressed no preference.
const HandsetProfile& DefaultHandsetProfile();

}  // namespace seoul

#endif  // SEOUL_BROWSER_HANDSET_HANDSET_TYPES_H_
