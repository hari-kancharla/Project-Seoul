// Project Seoul theme system.

#include "seoul/browser/themes/theme_validation.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace seoul {

namespace {

int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

double LinearChannel(uint8_t value) {
  const double s = value / 255.0;
  return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double RelativeLuminance(const ThemeColor &color) {
  return 0.2126 * LinearChannel(color.r) + 0.7152 * LinearChannel(color.g) +
         0.0722 * LinearChannel(color.b);
}

bool ValidTokenName(const std::string &name) {
  if (name.empty() || name.size() > kMaxTokenNameLength) {
    return false;
  }
  if (name[0] < 'a' || name[0] > 'z') {
    return false;
  }
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-')) {
      return false;
    }
  }
  return true;
}

bool ValidSlug(const std::string &slug) {
  return ValidTokenName(slug) && slug.size() <= kMaxThemeNameLength;
}

const char *SchemeToString(ColorScheme scheme) {
  switch (scheme) {
  case ColorScheme::kLight:
    return "light";
  case ColorScheme::kDark:
    return "dark";
  case ColorScheme::kSystem:
    return "system";
  }
  return "system";
}

bool SchemeFromString(const std::string &s, ColorScheme *out) {
  if (s == "light") {
    *out = ColorScheme::kLight;
  } else if (s == "dark") {
    *out = ColorScheme::kDark;
  } else if (s == "system") {
    *out = ColorScheme::kSystem;
  } else {
    return false;
  }
  return true;
}

} // namespace

bool ParseHexColor(const std::string &hex, ThemeColor *out) {
  if (hex.empty() || hex[0] != '#') {
    return false;
  }
  const std::string digits = hex.substr(1);
  auto read = [&digits](size_t index) { return HexDigit(digits[index]); };
  if (digits.size() == 3) {
    for (char c : digits) {
      if (HexDigit(c) < 0) {
        return false;
      }
    }
    out->r = static_cast<uint8_t>(read(0) * 17);
    out->g = static_cast<uint8_t>(read(1) * 17);
    out->b = static_cast<uint8_t>(read(2) * 17);
    out->a = 255;
    return true;
  }
  if (digits.size() == 6 || digits.size() == 8) {
    for (char c : digits) {
      if (HexDigit(c) < 0) {
        return false;
      }
    }
    out->r = static_cast<uint8_t>(read(0) * 16 + read(1));
    out->g = static_cast<uint8_t>(read(2) * 16 + read(3));
    out->b = static_cast<uint8_t>(read(4) * 16 + read(5));
    out->a =
        digits.size() == 8 ? static_cast<uint8_t>(read(6) * 16 + read(7)) : 255;
    return true;
  }
  return false;
}

std::string ColorToHex(const ThemeColor &color) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string hex = "#";
  for (uint8_t channel : {color.r, color.g, color.b, color.a}) {
    hex.push_back(kDigits[channel >> 4]);
    hex.push_back(kDigits[channel & 0x0f]);
  }
  return hex;
}

double ContrastRatio(const ThemeColor &a, const ThemeColor &b) {
  const double la = RelativeLuminance(a);
  const double lb = RelativeLuminance(b);
  const double lighter = std::max(la, lb);
  const double darker = std::min(la, lb);
  return (lighter + 0.05) / (darker + 0.05);
}

ThemeStatusResult ValidateTheme(const Theme &theme) {
  if (theme.schema_version != kThemeSchemaVersion) {
    return base::unexpected(ThemeError::kUnsupportedSchema);
  }
  if (!ValidSlug(theme.id) || theme.name.empty() ||
      theme.name.size() > kMaxThemeNameLength) {
    return base::unexpected(ThemeError::kInvalidName);
  }
  if (theme.typography.base_size_px < 8 || theme.typography.base_size_px > 48 ||
      theme.typography.scale_ratio < 1.0 ||
      theme.typography.scale_ratio > 2.0 ||
      theme.typography.base_line_height_permille < 1000 ||
      theme.typography.base_line_height_permille > 3000 ||
      theme.corner_radius_px < 0 || theme.corner_radius_px > 64) {
    return base::unexpected(ThemeError::kInvalidTypography);
  }
  if (theme.custom_colors.size() > kMaxCustomTokens) {
    return base::unexpected(ThemeError::kTooManyCustomTokens);
  }
  for (const auto &[name, color] : theme.custom_colors) {
    if (!ValidTokenName(name)) {
      return base::unexpected(ThemeError::kInvalidToken);
    }
  }
  // Contrast gates. Normal text needs AA; muted text and borders need at
  // least the large-text/UI threshold.
  if (ContrastRatio(theme.colors.text, theme.colors.background) <
      kMinAccessibleContrast) {
    return base::unexpected(ThemeError::kContrastTooLow);
  }
  if (ContrastRatio(theme.colors.text, theme.colors.surface) <
      kMinAccessibleContrast) {
    return base::unexpected(ThemeError::kContrastTooLow);
  }
  if (ContrastRatio(theme.colors.accent_text, theme.colors.accent) <
      kMinAccessibleContrast) {
    return base::unexpected(ThemeError::kContrastTooLow);
  }
  if (ContrastRatio(theme.colors.muted_text, theme.colors.background) <
      kMinLargeTextContrast) {
    return base::unexpected(ThemeError::kContrastTooLow);
  }
  if (ContrastRatio(theme.colors.error, theme.colors.background) <
      kMinLargeTextContrast) {
    return base::unexpected(ThemeError::kContrastTooLow);
  }
  return base::ok();
}

base::DictValue ThemeToValue(const Theme &theme) {
  base::DictValue dict;
  dict.Set("schema_version", theme.schema_version);
  dict.Set("id", theme.id);
  dict.Set("name", theme.name);
  dict.Set("scheme", SchemeToString(theme.scheme));
  base::DictValue colors;
  colors.Set("background", ColorToHex(theme.colors.background));
  colors.Set("surface", ColorToHex(theme.colors.surface));
  colors.Set("text", ColorToHex(theme.colors.text));
  colors.Set("muted_text", ColorToHex(theme.colors.muted_text));
  colors.Set("accent", ColorToHex(theme.colors.accent));
  colors.Set("accent_text", ColorToHex(theme.colors.accent_text));
  colors.Set("border", ColorToHex(theme.colors.border));
  colors.Set("error", ColorToHex(theme.colors.error));
  dict.Set("colors", std::move(colors));
  base::DictValue typography;
  typography.Set("font_family", theme.typography.font_family);
  typography.Set("base_size_px", theme.typography.base_size_px);
  typography.Set("scale_ratio", theme.typography.scale_ratio);
  typography.Set("base_line_height_permille",
                 theme.typography.base_line_height_permille);
  dict.Set("typography", std::move(typography));
  base::DictValue motion;
  motion.Set("reduced_motion", theme.motion.reduced_motion);
  motion.Set("reduced_transparency", theme.motion.reduced_transparency);
  motion.Set("base_duration_ms", theme.motion.base_duration_ms);
  dict.Set("motion", std::move(motion));
  dict.Set("corner_radius_px", theme.corner_radius_px);
  if (!theme.custom_colors.empty()) {
    base::DictValue custom;
    for (const auto &[name, color] : theme.custom_colors) {
      custom.Set(name, ColorToHex(color));
    }
    dict.Set("custom_colors", std::move(custom));
  }
  return dict;
}

ThemeResult<Theme> ThemeFromValue(const base::Value &value) {
  const base::DictValue *dict = value.GetIfDict();
  if (!dict) {
    return base::unexpected(ThemeError::kUnsupportedSchema);
  }
  if (dict->FindInt("schema_version").value_or(0) != kThemeSchemaVersion) {
    return base::unexpected(ThemeError::kUnsupportedSchema);
  }
  Theme theme;
  const std::string *id = dict->FindString("id");
  const std::string *name = dict->FindString("name");
  if (!id || !name) {
    return base::unexpected(ThemeError::kInvalidName);
  }
  theme.id = *id;
  theme.name = *name;
  if (const std::string *scheme = dict->FindString("scheme")) {
    if (!SchemeFromString(*scheme, &theme.scheme)) {
      return base::unexpected(ThemeError::kInvalidToken);
    }
  }
  const base::DictValue *colors = dict->FindDict("colors");
  if (!colors) {
    return base::unexpected(ThemeError::kInvalidColor);
  }
  auto read_color = [&colors](const char *key, ThemeColor *out) {
    const std::string *hex = colors->FindString(key);
    return hex && ParseHexColor(*hex, out);
  };
  if (!read_color("background", &theme.colors.background) ||
      !read_color("surface", &theme.colors.surface) ||
      !read_color("text", &theme.colors.text) ||
      !read_color("muted_text", &theme.colors.muted_text) ||
      !read_color("accent", &theme.colors.accent) ||
      !read_color("accent_text", &theme.colors.accent_text) ||
      !read_color("border", &theme.colors.border) ||
      !read_color("error", &theme.colors.error)) {
    return base::unexpected(ThemeError::kInvalidColor);
  }
  if (const base::DictValue *typography = dict->FindDict("typography")) {
    if (const std::string *font = typography->FindString("font_family")) {
      theme.typography.font_family = *font;
    }
    theme.typography.base_size_px =
        typography->FindInt("base_size_px").value_or(14);
    theme.typography.scale_ratio =
        typography->FindDouble("scale_ratio").value_or(1.2);
    theme.typography.base_line_height_permille =
        typography->FindInt("base_line_height_permille").value_or(1500);
  }
  if (const base::DictValue *motion = dict->FindDict("motion")) {
    theme.motion.reduced_motion =
        motion->FindBool("reduced_motion").value_or(false);
    theme.motion.reduced_transparency =
        motion->FindBool("reduced_transparency").value_or(false);
    theme.motion.base_duration_ms =
        motion->FindInt("base_duration_ms").value_or(150);
  }
  theme.corner_radius_px = dict->FindInt("corner_radius_px").value_or(8);
  if (const base::DictValue *custom = dict->FindDict("custom_colors")) {
    for (const auto [key, color_value] : *custom) {
      ThemeColor color;
      if (!color_value.is_string() ||
          !ParseHexColor(color_value.GetString(), &color)) {
        return base::unexpected(ThemeError::kInvalidColor);
      }
      theme.custom_colors[key] = color;
    }
  }
  if (auto valid = ValidateTheme(theme); !valid.has_value()) {
    return base::unexpected(valid.error());
  }
  return theme;
}

const char *ThemeErrorToString(ThemeError error) {
  switch (error) {
  case ThemeError::kInvalidName:
    return "invalid_name";
  case ThemeError::kInvalidToken:
    return "invalid_token";
  case ThemeError::kInvalidColor:
    return "invalid_color";
  case ThemeError::kTooManyCustomTokens:
    return "too_many_custom_tokens";
  case ThemeError::kContrastTooLow:
    return "contrast_too_low";
  case ThemeError::kUnsupportedSchema:
    return "unsupported_schema";
  case ThemeError::kInvalidTypography:
    return "invalid_typography";
  case ThemeError::kUnknownTheme:
    return "unknown_theme";
  case ThemeError::kLimitExceeded:
    return "limit_exceeded";
  case ThemeError::kInUse:
    return "in_use";
  }
  return "invalid_token";
}

} // namespace seoul
