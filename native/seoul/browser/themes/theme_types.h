// Project Seoul theme system.
// Theme token model and results. A theme is a bounded set of design tokens
// (colors, typography, radii, motion) plus accessibility preferences. Themes
// apply globally or per Scene; contrast is validated before a theme is
// accepted so a theme can never make text unreadable.

#ifndef SEOUL_BROWSER_THEMES_THEME_TYPES_H_
#define SEOUL_BROWSER_THEMES_THEME_TYPES_H_

#include <cstdint>
#include <map>
#include <string>

#include "base/types/expected.h"

namespace seoul {

inline constexpr int kThemeSchemaVersion = 1;
inline constexpr size_t kMaxThemes = 128;
inline constexpr size_t kMaxThemeNameLength = 120;
inline constexpr size_t kMaxCustomTokens = 64;
inline constexpr size_t kMaxTokenNameLength = 64;

enum class ColorScheme {
  kLight,
  kDark,
  kSystem,
};

// sRGB color with alpha. Stored as bytes so serialization is exact.
struct ThemeColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  friend bool operator==(const ThemeColor &, const ThemeColor &) = default;
};

// Parses "#rgb", "#rrggbb", or "#rrggbbaa" (leading '#', hex only).
bool ParseHexColor(const std::string &hex, ThemeColor *out);
std::string ColorToHex(const ThemeColor &color);

// The required color roles every theme must define. Custom tokens extend but
// never replace these.
struct ThemeColorRoles {
  ThemeColor background;
  ThemeColor surface;
  ThemeColor text;
  ThemeColor muted_text;
  ThemeColor accent;
  ThemeColor accent_text; // text drawn on accent
  ThemeColor border;
  ThemeColor error;

  friend bool operator==(const ThemeColorRoles &,
                         const ThemeColorRoles &) = default;
};

struct ThemeTypography {
  ThemeTypography();
  ThemeTypography(const ThemeTypography &);
  ThemeTypography(ThemeTypography &&);
  ThemeTypography &operator=(const ThemeTypography &);
  ThemeTypography &operator=(ThemeTypography &&);
  ~ThemeTypography();

  std::string font_family; // family name only; never a URL or @font-face
  int base_size_px = 14;
  double scale_ratio = 1.2;             // modular scale for headings
  int base_line_height_permille = 1500; // 1.5 as parts-per-thousand

  friend bool operator==(const ThemeTypography &,
                         const ThemeTypography &) = default;
};

struct ThemeMotion {
  bool reduced_motion = false;
  bool reduced_transparency = false;
  int base_duration_ms = 150;

  friend bool operator==(const ThemeMotion &, const ThemeMotion &) = default;
};

struct Theme {
  Theme();
  Theme(const Theme &);
  Theme(Theme &&);
  Theme &operator=(const Theme &);
  Theme &operator=(Theme &&);
  ~Theme();

  int schema_version = kThemeSchemaVersion;
  std::string id; // stable slug
  std::string name;
  ColorScheme scheme = ColorScheme::kSystem;
  ThemeColorRoles colors;
  ThemeTypography typography;
  ThemeMotion motion;
  int corner_radius_px = 8;
  std::map<std::string, ThemeColor> custom_colors; // name -> color

  friend bool operator==(const Theme &, const Theme &) = default;
};

enum class ThemeError {
  kInvalidName,
  kInvalidToken,
  kInvalidColor,
  kTooManyCustomTokens,
  kContrastTooLow,
  kUnsupportedSchema,
  kInvalidTypography,
  kUnknownTheme,
  kLimitExceeded,
  kInUse,
};

const char *ThemeErrorToString(ThemeError error);

template <typename T> using ThemeResult = base::expected<T, ThemeError>;

using ThemeStatusResult = base::expected<void, ThemeError>;

} // namespace seoul

#endif // SEOUL_BROWSER_THEMES_THEME_TYPES_H_
