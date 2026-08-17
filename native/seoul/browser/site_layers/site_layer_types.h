// Project Seoul Site Layers.
// Declarative, per-site visual customization. A Site Layer is a bounded list
// of typed adjustments (color, typography, width, spacing, density, hide,
// emphasize, sticky, reading mode, accessibility) scoped to an origin and
// optionally a Scene. Layers compile to safe scoped CSS; they never carry or
// generate JavaScript, and selectors/values are validated to prevent style
// injection or escaping the target document.

#ifndef SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_TYPES_H_
#define SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_TYPES_H_

#include <string>
#include <vector>

#include "base/types/expected.h"

namespace seoul {

inline constexpr int kSiteLayerSchemaVersion = 1;
inline constexpr size_t kMaxLayerRules = 64;
inline constexpr size_t kMaxSelectorLength = 256;
inline constexpr size_t kMaxSelectorsPerRule = 8;
inline constexpr size_t kMaxLayerNameLength = 120;
inline constexpr size_t kMaxOriginPatternLength = 256;
inline constexpr size_t kMaxSiteLayers = 256;

// Arc's global switch, Settings > Advanced: "Enable Boosts on websites you
// visit." One profile pref, checked at the single point every Boost is
// applied, so turning it off silences every Boost at once without deleting
// any of them.
inline constexpr char kSeoulBoostsEnabledPref[] = "seoul.boosts.enabled";

// Arc's Code editor writes raw CSS and raw JavaScript. JavaScript is a
// different kind of power from every other adjustment - the typed vocabulary
// compiles to validated CSS and can touch nothing else - so it is gated on its
// own switch, per profile, defaulting OFF. That is Arc's own current posture:
// it disabled JavaScript Boosts and made you re-enable the ones you want.
inline constexpr char kSeoulBoostJavaScriptEnabledPref[] =
    "seoul.boosts.javascript_enabled";

// Bounds on the Code editor's two documents. Large enough for a real
// user stylesheet or script, small enough that a layer cannot become an
// unbounded blob in the profile.
inline constexpr size_t kMaxCustomCssLength = 64u * 1024u;
inline constexpr size_t kMaxCustomJavaScriptLength = 64u * 1024u;

enum class SiteAdjustmentKind {
  kAccentColor, // recolor accent/link elements
  kBackgroundColor,
  kTextColor,
  kTintColor,        // document tint with strength in [0.05, 0.75]
  kFontFamily,       // family name only
  kFontSizeScale,    // multiplier in [0.5, 2.0]
  kContentWidth,     // max content width in px
  kLineSpacing,      // multiplier in [1.0, 3.0]
  kDensity,          // compact / comfortable / spacious
  kHide,             // display:none for matched elements
  kEmphasize,        // outline/weight emphasis
  kStickyHeaderOff,  // neutralize position:sticky/fixed on matched elements
  kReadingMode,      // document-level readable layout
  kIncreaseContrast, // accessibility contrast boost
  kReduceMotion,     // accessibility motion reduction
  // Arc's three "Advanced color controls" sliders. Each is a document-level
  // multiplier in [0.0, 2.0] over the rendered page. They compile into one
  // combined `filter` declaration, because CSS `filter` does not accumulate
  // across rules - a second declaration replaces the first rather than
  // composing with it.
  kContrastLevel,
  kBrightnessLevel,
  kSaturationLevel,
  kTextCase, // Arc's "Case": capitalization applied to all text
  // Follows the browser/system color scheme: Blink's automatic darkening is
  // enabled only while the current browser color mode is dark.
  kAutomaticDarkMode,
};

enum class DensityLevel {
  kCompact,
  kComfortable,
  kSpacious,
};

// Arc's "Case" control. kOriginal leaves the page's own capitalization alone.
enum class TextCase {
  kOriginal,
  kUpper,
  kLower,
  kTitle,
};

struct SiteAdjustment {
  SiteAdjustment();
  SiteAdjustment(const SiteAdjustment &);
  SiteAdjustment(SiteAdjustment &&);
  SiteAdjustment &operator=(const SiteAdjustment &);
  SiteAdjustment &operator=(SiteAdjustment &&);
  ~SiteAdjustment();

  SiteAdjustmentKind kind = SiteAdjustmentKind::kReadingMode;
  // Target selectors. Empty means document-level (only valid for the
  // document-scoped kinds: reading mode, contrast, motion, width, tint, and
  // automatic dark mode). Font family accepts either document scope or an
  // explicit target. Non-empty selectors are validated to a safe subset.
  std::vector<std::string> selectors;
  std::string color_value;    // "#rrggbb"/"#rrggbbaa" for color kinds
  std::string font_family;    // family name for kFontFamily
  double numeric_value = 0.0; // scale/width/spacing/filter level per kind
  DensityLevel density = DensityLevel::kComfortable;
  TextCase text_case = TextCase::kOriginal; // for kTextCase

  friend bool operator==(const SiteAdjustment &,
                         const SiteAdjustment &) = default;
};

struct SiteLayer {
  SiteLayer();
  SiteLayer(const SiteLayer &);
  SiteLayer(SiteLayer &&);
  SiteLayer &operator=(const SiteLayer &);
  SiteLayer &operator=(SiteLayer &&);
  ~SiteLayer();

  int schema_version = kSiteLayerSchemaVersion;
  std::string id;
  std::string name;
  // Exact web origin ("https://example.com", "http://localhost:3000") or a
  // scheme-agnostic host wildcard ("*.example.com").
  std::string origin_pattern;
  std::string scene_scope; // optional Scene id
  bool enabled = true;
  std::vector<SiteAdjustment> adjustments;
  // Arc's Code editor. Raw author CSS, appended after the compiled typed
  // adjustments so it wins on equal specificity, and raw author JavaScript,
  // which runs only when the JavaScript switch is on.
  std::string custom_css;
  std::string custom_javascript;

  friend bool operator==(const SiteLayer &, const SiteLayer &) = default;
};

enum class SiteLayerError {
  kInvalidId,
  kInvalidName,
  kInvalidOrigin,
  kEmptyLayer,
  kCustomCodeTooLong,
  kTooManyRules,
  kInvalidSelector,
  kUnsafeSelector,
  kInvalidColor,
  kInvalidFontFamily,
  kInvalidNumericValue,
  kSelectorRequired,
  kSelectorNotAllowed,
  kUnsupportedSchema,
  kUnknownLayer,
  kLimitExceeded,
  kInUse,
};

const char *SiteLayerErrorToString(SiteLayerError error);

template <typename T> using SiteLayerResult = base::expected<T, SiteLayerError>;

using SiteLayerStatusResult = base::expected<void, SiteLayerError>;

} // namespace seoul

#endif // SEOUL_BROWSER_SITE_LAYERS_SITE_LAYER_TYPES_H_
