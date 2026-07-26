// Project Seoul Site Layers.

#include "seoul/browser/site_layers/site_layer_compiler.h"

#include <cmath>
#include <string_view>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

// Characters that can escape a CSS declaration or introduce script. Any of
// these in a selector or value rejects the whole layer.
bool ContainsUnsafeCssChar(const std::string &text,
                           bool allow_parentheses = false) {
  for (char c : text) {
    switch (c) {
    case '{':
    case '}':
    case ';':
    case '<':
    case '\\':
    case '"':
    case '\'':
    case '@':
    case '(':
    case ')':
      if (allow_parentheses) {
        break;
      }
      return true;
    case '`':
      return true;
    default:
      break;
    }
  }
  // Reject comment sequences and known dangerous tokens defensively.
  if (text.find("/*") != std::string::npos ||
      text.find("*/") != std::string::npos) {
    return true;
  }
  const std::string lowered = base::ToLowerASCII(text);
  return lowered.find("url") != std::string::npos ||
         lowered.find("expression") != std::string::npos ||
         lowered.find("javascript") != std::string::npos ||
         lowered.find("import") != std::string::npos;
}

bool ValidColorValue(const std::string &hex) {
  if (hex.empty() || hex[0] != '#') {
    return false;
  }
  const std::string digits = hex.substr(1);
  if (digits.size() != 3 && digits.size() != 6 && digits.size() != 8) {
    return false;
  }
  for (char c : digits) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

bool ValidFontFamily(const std::string &family) {
  if (family.empty() || family.size() > 64 || ContainsUnsafeCssChar(family)) {
    return false;
  }
  for (char c : family) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == ' ' || c == '-')) {
      return false;
    }
  }
  return true;
}

// Adjustments that apply to the whole document and reject selectors.
bool SelectorsForbidden(SiteAdjustmentKind kind) {
  switch (kind) {
  case SiteAdjustmentKind::kReadingMode:
  case SiteAdjustmentKind::kIncreaseContrast:
  case SiteAdjustmentKind::kReduceMotion:
  case SiteAdjustmentKind::kContentWidth:
  case SiteAdjustmentKind::kDensity:
  case SiteAdjustmentKind::kTintColor:
  case SiteAdjustmentKind::kAutomaticDarkMode:
    return true;
  default:
    return false;
  }
}

bool NeedsColor(SiteAdjustmentKind kind) {
  return kind == SiteAdjustmentKind::kAccentColor ||
         kind == SiteAdjustmentKind::kBackgroundColor ||
         kind == SiteAdjustmentKind::kTextColor ||
         kind == SiteAdjustmentKind::kTintColor;
}

SiteLayerStatusResult ValidateAdjustment(const SiteAdjustment &adjustment) {
  if (SelectorsForbidden(adjustment.kind)) {
    if (!adjustment.selectors.empty()) {
      return base::unexpected(SiteLayerError::kSelectorNotAllowed);
    }
  } else if (adjustment.kind != SiteAdjustmentKind::kFontFamily) {
    if (adjustment.selectors.empty()) {
      return base::unexpected(SiteLayerError::kSelectorRequired);
    }
  }
  if (adjustment.selectors.size() > kMaxSelectorsPerRule) {
    return base::unexpected(SiteLayerError::kTooManyRules);
  }
  for (const std::string &selector : adjustment.selectors) {
    if (selector.size() > kMaxSelectorLength) {
      return base::unexpected(SiteLayerError::kInvalidSelector);
    }
    if (!IsSafeSelector(selector)) {
      return base::unexpected(SiteLayerError::kUnsafeSelector);
    }
  }
  if (NeedsColor(adjustment.kind) && !ValidColorValue(adjustment.color_value)) {
    return base::unexpected(SiteLayerError::kInvalidColor);
  }
  if (adjustment.kind == SiteAdjustmentKind::kFontFamily &&
      !ValidFontFamily(adjustment.font_family)) {
    return base::unexpected(SiteLayerError::kInvalidFontFamily);
  }
  auto in_range = [](double v, double lo, double hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
  };
  switch (adjustment.kind) {
  case SiteAdjustmentKind::kFontSizeScale:
    if (!in_range(adjustment.numeric_value, 0.5, 2.0)) {
      return base::unexpected(SiteLayerError::kInvalidNumericValue);
    }
    break;
  case SiteAdjustmentKind::kContentWidth:
    if (!in_range(adjustment.numeric_value, 320.0, 2000.0)) {
      return base::unexpected(SiteLayerError::kInvalidNumericValue);
    }
    break;
  case SiteAdjustmentKind::kLineSpacing:
    if (!in_range(adjustment.numeric_value, 1.0, 3.0)) {
      return base::unexpected(SiteLayerError::kInvalidNumericValue);
    }
    break;
  case SiteAdjustmentKind::kTintColor:
    if (!in_range(adjustment.numeric_value, 0.05, 0.75)) {
      return base::unexpected(SiteLayerError::kInvalidNumericValue);
    }
    break;
  default:
    break;
  }
  return base::ok();
}

std::string JoinSelectors(const std::vector<std::string> &selectors) {
  std::string joined;
  for (size_t i = 0; i < selectors.size(); ++i) {
    if (i != 0) {
      joined += ", ";
    }
    joined += selectors[i];
  }
  return joined;
}

std::string FormatNumber(double value) {
  std::string formatted = base::NumberToString(value);
  return formatted;
}

std::string CompileAdjustment(const SiteAdjustment &adjustment) {
  const std::string selector = JoinSelectors(adjustment.selectors);
  switch (adjustment.kind) {
  case SiteAdjustmentKind::kAccentColor:
    return selector + " { color: " + adjustment.color_value +
           " !important; }\n";
  case SiteAdjustmentKind::kBackgroundColor:
    return selector + " { background-color: " + adjustment.color_value +
           " !important; }\n";
  case SiteAdjustmentKind::kTextColor:
    return selector + " { color: " + adjustment.color_value +
           " !important; }\n";
  case SiteAdjustmentKind::kTintColor:
    // A fixed, input-transparent blend layer tints the rendered document
    // without intercepting page interaction or commandeering a site's own
    // pseudo-elements. The applicator owns this inert custom element.
    return "html > div[data-seoul-browser-boost-tint-v1] { display: block "
           "!important; position: fixed !important; inset: 0 !important; "
           "pointer-events: none !important; z-index: 2147483646 !important; "
           "background: " +
           adjustment.color_value +
           " !important; opacity: " + FormatNumber(adjustment.numeric_value) +
           " !important; mix-blend-mode: color !important; }\n";
  case SiteAdjustmentKind::kFontFamily:
    return (selector.empty() ? "html, body, body *, input, button, textarea, "
                               "select"
                             : selector) +
           " { font-family: " + adjustment.font_family +
           ", sans-serif !important; }\n";
  case SiteAdjustmentKind::kFontSizeScale:
    return selector +
           " { font-size: " + FormatNumber(adjustment.numeric_value) +
           "em !important; }\n";
  case SiteAdjustmentKind::kContentWidth:
    return "html body { max-width: " + FormatNumber(adjustment.numeric_value) +
           "px !important; margin-left: auto !important; "
           "margin-right: auto !important; }\n";
  case SiteAdjustmentKind::kLineSpacing:
    return selector +
           " { line-height: " + FormatNumber(adjustment.numeric_value) +
           " !important; }\n";
  case SiteAdjustmentKind::kDensity: {
    const char *padding = adjustment.density == DensityLevel::kCompact ? "4px"
                          : adjustment.density == DensityLevel::kSpacious
                              ? "16px"
                              : "8px";
    return std::string("html body * { padding: ") + padding +
           " !important; }\n";
  }
  case SiteAdjustmentKind::kHide:
    return selector + " { display: none !important; }\n";
  case SiteAdjustmentKind::kEmphasize:
    return selector + " { outline: 2px solid currentColor !important; "
                      "font-weight: 600 !important; }\n";
  case SiteAdjustmentKind::kStickyHeaderOff:
    return selector + " { position: static !important; }\n";
  case SiteAdjustmentKind::kReadingMode:
    return "html body { max-width: 720px !important; margin: 0 auto "
           "!important; line-height: 1.6 !important; }\n";
  case SiteAdjustmentKind::kIncreaseContrast:
    return "html { filter: contrast(1.2) !important; }\n";
  case SiteAdjustmentKind::kReduceMotion:
    return "*, *::before, *::after { animation-duration: 0s !important; "
           "transition-duration: 0s !important; }\n";
  case SiteAdjustmentKind::kAutomaticDarkMode:
    // Applied through Blink WebPreferences by the live applicator. Keeping it
    // in the typed layer makes persistence, matching, and toggling atomic.
    return std::string();
  }
  return std::string();
}

const char *AdjustmentKindName(SiteAdjustmentKind kind) {
  switch (kind) {
  case SiteAdjustmentKind::kAccentColor:
    return "accent_color";
  case SiteAdjustmentKind::kBackgroundColor:
    return "background_color";
  case SiteAdjustmentKind::kTextColor:
    return "text_color";
  case SiteAdjustmentKind::kTintColor:
    return "tint_color";
  case SiteAdjustmentKind::kFontFamily:
    return "font_family";
  case SiteAdjustmentKind::kFontSizeScale:
    return "font_size_scale";
  case SiteAdjustmentKind::kContentWidth:
    return "content_width";
  case SiteAdjustmentKind::kLineSpacing:
    return "line_spacing";
  case SiteAdjustmentKind::kDensity:
    return "density";
  case SiteAdjustmentKind::kHide:
    return "hide";
  case SiteAdjustmentKind::kEmphasize:
    return "emphasize";
  case SiteAdjustmentKind::kStickyHeaderOff:
    return "sticky_header_off";
  case SiteAdjustmentKind::kReadingMode:
    return "reading_mode";
  case SiteAdjustmentKind::kIncreaseContrast:
    return "increase_contrast";
  case SiteAdjustmentKind::kReduceMotion:
    return "reduce_motion";
  case SiteAdjustmentKind::kAutomaticDarkMode:
    return "automatic_dark_mode";
  }
  return "reading_mode";
}

bool AdjustmentKindFromName(const std::string &name, SiteAdjustmentKind *out) {
  static constexpr std::pair<const char *, SiteAdjustmentKind> kKinds[] = {
      {"accent_color", SiteAdjustmentKind::kAccentColor},
      {"background_color", SiteAdjustmentKind::kBackgroundColor},
      {"text_color", SiteAdjustmentKind::kTextColor},
      {"tint_color", SiteAdjustmentKind::kTintColor},
      {"font_family", SiteAdjustmentKind::kFontFamily},
      {"font_size_scale", SiteAdjustmentKind::kFontSizeScale},
      {"content_width", SiteAdjustmentKind::kContentWidth},
      {"line_spacing", SiteAdjustmentKind::kLineSpacing},
      {"density", SiteAdjustmentKind::kDensity},
      {"hide", SiteAdjustmentKind::kHide},
      {"emphasize", SiteAdjustmentKind::kEmphasize},
      {"sticky_header_off", SiteAdjustmentKind::kStickyHeaderOff},
      {"reading_mode", SiteAdjustmentKind::kReadingMode},
      {"increase_contrast", SiteAdjustmentKind::kIncreaseContrast},
      {"reduce_motion", SiteAdjustmentKind::kReduceMotion},
      {"automatic_dark_mode", SiteAdjustmentKind::kAutomaticDarkMode},
  };
  for (const auto &[kind_name, kind] : kKinds) {
    if (name == kind_name) {
      *out = kind;
      return true;
    }
  }
  return false;
}

const char *DensityName(DensityLevel level) {
  switch (level) {
  case DensityLevel::kCompact:
    return "compact";
  case DensityLevel::kComfortable:
    return "comfortable";
  case DensityLevel::kSpacious:
    return "spacious";
  }
  return "comfortable";
}

bool DensityFromName(const std::string &name, DensityLevel *out) {
  if (name == "compact") {
    *out = DensityLevel::kCompact;
  } else if (name == "comfortable") {
    *out = DensityLevel::kComfortable;
  } else if (name == "spacious") {
    *out = DensityLevel::kSpacious;
  } else {
    return false;
  }
  return true;
}

} // namespace

bool IsSafeSelector(const std::string &selector) {
  if (selector.empty() || selector.size() > kMaxSelectorLength ||
      ContainsUnsafeCssChar(selector, /*allow_parentheses=*/true)) {
    return false;
  }
  // Parentheses are accepted only as part of :nth-of-type(N), where N is a
  // non-zero decimal integer. This is the exact fallback emitted by Zap when
  // an element has no stable id/class path.
  for (size_t i = 0; i < selector.size(); ++i) {
    if (selector[i] != '(' && selector[i] != ')') {
      continue;
    }
    constexpr std::string_view kPrefix = ":nth-of-type(";
    const size_t prefix_start =
        i >= kPrefix.size() - 1 ? i - (kPrefix.size() - 1) : std::string::npos;
    if (selector[i] != '(' || prefix_start == std::string::npos ||
        selector.compare(prefix_start, kPrefix.size(), kPrefix) != 0) {
      return false;
    }
    size_t digit = i + 1;
    if (digit >= selector.size() || selector[digit] < '1' ||
        selector[digit] > '9') {
      return false;
    }
    while (digit < selector.size() && selector[digit] >= '0' &&
           selector[digit] <= '9') {
      ++digit;
    }
    if (digit >= selector.size() || selector[digit] != ')') {
      return false;
    }
    i = digit;
  }
  // Allowed: letters, digits, '.', '#', '-', '_', '[', ']', '=', '*', '~',
  // '^', '$', '|', ':', space, '>', '+'. Attribute values are quoted normally
  // but quotes are rejected above, so only presence/prefix attribute forms
  // survive; that is acceptable for the safe subset.
  for (char c : selector) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '.' || c == '#' ||
                         c == '-' || c == '_' || c == '[' || c == ']' ||
                         c == '=' || c == '*' || c == '~' || c == '^' ||
                         c == '$' || c == '|' || c == ':' || c == ' ' ||
                         c == '>' || c == '+' || c == '(' || c == ')';
    if (!allowed) {
      return false;
    }
  }
  // A bare universal or empty-ish selector is not useful and risks
  // over-broad rules; require at least one identifier character.
  bool has_identifier = false;
  for (char c : selector) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
      has_identifier = true;
      break;
    }
  }
  return has_identifier;
}

bool IsValidOriginPattern(const std::string &origin) {
  if (origin.empty() || origin.size() > kMaxOriginPatternLength) {
    return false;
  }
  std::string host;
  if (base::StartsWith(origin, "*.")) {
    host = origin.substr(2);
  } else {
    for (const unsigned char character : origin) {
      if (character <= 0x20 || character == 0x7f) {
        return false;
      }
    }
    const size_t scheme_end = origin.find("://");
    if (scheme_end == std::string::npos) {
      return false;
    }
    const std::string authority = origin.substr(scheme_end + 3);
    if (authority.empty() ||
        authority.find_first_of("/?#@") != std::string::npos) {
      return false;
    }
    if (authority.front() == '[') {
      const size_t closing_bracket = authority.find(']');
      if (closing_bracket == std::string::npos ||
          (closing_bracket + 1 < authority.size() &&
           authority[closing_bracket + 1] != ':')) {
        return false;
      }
    } else {
      const size_t colon = authority.rfind(':');
      const std::string raw_host = authority.substr(0, colon);
      if (raw_host.empty() || raw_host.front() == '.' ||
          raw_host.back() == '.' || raw_host.find("..") != std::string::npos) {
        return false;
      }
    }
    const GURL url(origin);
    const url::Origin parsed = url::Origin::Create(url);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || parsed.opaque() ||
        url.has_username() || url.has_password() || url.has_query() ||
        url.has_ref() || url.path() != "/") {
      return false;
    }
    if (url.has_port() && url.IntPort() <= 0) {
      return false;
    }
    return true;
  }
  if (host.empty() || host.size() > 253) {
    return false;
  }
  // Host label charset: letters, digits, '-', '.'; no consecutive dots.
  char previous = 0;
  for (char c : host) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!allowed) {
      return false;
    }
    if (c == '.' && previous == '.') {
      return false;
    }
    previous = c;
  }
  return host.front() != '.' && host.back() != '.';
}

bool IsValidSiteLayerId(const std::string &id) {
  if (id.empty() || id.size() > kMaxLayerNameLength) {
    return false;
  }
  if (id[0] < 'a' || id[0] > 'z') {
    return false;
  }
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-')) {
      return false;
    }
  }
  return true;
}

SiteLayerStatusResult ValidateSiteLayer(const SiteLayer &layer) {
  if (layer.schema_version != kSiteLayerSchemaVersion) {
    return base::unexpected(SiteLayerError::kUnsupportedSchema);
  }
  if (!IsValidSiteLayerId(layer.id)) {
    return base::unexpected(SiteLayerError::kInvalidId);
  }
  if (layer.name.empty() || layer.name.size() > kMaxLayerNameLength) {
    return base::unexpected(SiteLayerError::kInvalidName);
  }
  if (!IsValidOriginPattern(layer.origin_pattern)) {
    return base::unexpected(SiteLayerError::kInvalidOrigin);
  }
  if (layer.adjustments.size() > kMaxLayerRules) {
    return base::unexpected(SiteLayerError::kTooManyRules);
  }
  for (const SiteAdjustment &adjustment : layer.adjustments) {
    if (auto result = ValidateAdjustment(adjustment); !result.has_value()) {
      return result;
    }
  }
  return base::ok();
}

SiteLayerResult<std::string> CompileSiteLayer(const SiteLayer &layer) {
  if (auto valid = ValidateSiteLayer(layer); !valid.has_value()) {
    return base::unexpected(valid.error());
  }
  std::string css;
  for (const SiteAdjustment &adjustment : layer.adjustments) {
    css += CompileAdjustment(adjustment);
  }
  return css;
}

base::DictValue SiteLayerToValue(const SiteLayer &layer) {
  base::DictValue dict;
  dict.Set("schema_version", layer.schema_version);
  dict.Set("id", layer.id);
  dict.Set("name", layer.name);
  dict.Set("origin_pattern", layer.origin_pattern);
  if (!layer.scene_scope.empty()) {
    dict.Set("scene_scope", layer.scene_scope);
  }
  dict.Set("enabled", layer.enabled);
  base::ListValue adjustments;
  for (const SiteAdjustment &adjustment : layer.adjustments) {
    base::DictValue adjustment_dict;
    adjustment_dict.Set("kind", AdjustmentKindName(adjustment.kind));
    if (!adjustment.selectors.empty()) {
      base::ListValue selectors;
      for (const std::string &selector : adjustment.selectors) {
        selectors.Append(selector);
      }
      adjustment_dict.Set("selectors", std::move(selectors));
    }
    if (!adjustment.color_value.empty()) {
      adjustment_dict.Set("color_value", adjustment.color_value);
    }
    if (!adjustment.font_family.empty()) {
      adjustment_dict.Set("font_family", adjustment.font_family);
    }
    if (adjustment.numeric_value != 0.0) {
      adjustment_dict.Set("numeric_value", adjustment.numeric_value);
    }
    if (adjustment.kind == SiteAdjustmentKind::kDensity) {
      adjustment_dict.Set("density", DensityName(adjustment.density));
    }
    adjustments.Append(std::move(adjustment_dict));
  }
  dict.Set("adjustments", std::move(adjustments));
  return dict;
}

SiteLayerResult<SiteLayer> SiteLayerFromValue(const base::Value &value) {
  const base::DictValue *dict = value.GetIfDict();
  if (!dict) {
    return base::unexpected(SiteLayerError::kUnsupportedSchema);
  }
  if (dict->FindInt("schema_version").value_or(0) != kSiteLayerSchemaVersion) {
    return base::unexpected(SiteLayerError::kUnsupportedSchema);
  }
  SiteLayer layer;
  if (const std::string *id = dict->FindString("id")) {
    layer.id = *id;
  }
  const std::string *name = dict->FindString("name");
  const std::string *origin = dict->FindString("origin_pattern");
  if (!name || !origin) {
    return base::unexpected(SiteLayerError::kInvalidName);
  }
  layer.name = *name;
  layer.origin_pattern = *origin;
  if (const std::string *scene = dict->FindString("scene_scope")) {
    layer.scene_scope = *scene;
  }
  layer.enabled = dict->FindBool("enabled").value_or(true);
  const base::ListValue *adjustments = dict->FindList("adjustments");
  if (!adjustments) {
    return base::unexpected(SiteLayerError::kEmptyLayer);
  }
  for (const base::Value &adjustment_value : *adjustments) {
    const base::DictValue *adjustment_dict = adjustment_value.GetIfDict();
    if (!adjustment_dict) {
      return base::unexpected(SiteLayerError::kInvalidSelector);
    }
    SiteAdjustment adjustment;
    const std::string *kind = adjustment_dict->FindString("kind");
    if (!kind || !AdjustmentKindFromName(*kind, &adjustment.kind)) {
      return base::unexpected(SiteLayerError::kInvalidSelector);
    }
    if (const base::ListValue *selectors =
            adjustment_dict->FindList("selectors")) {
      for (const base::Value &selector : *selectors) {
        if (!selector.is_string()) {
          return base::unexpected(SiteLayerError::kInvalidSelector);
        }
        adjustment.selectors.push_back(selector.GetString());
      }
    }
    if (const std::string *color = adjustment_dict->FindString("color_value")) {
      adjustment.color_value = *color;
    }
    if (const std::string *font = adjustment_dict->FindString("font_family")) {
      adjustment.font_family = *font;
    }
    adjustment.numeric_value =
        adjustment_dict->FindDouble("numeric_value").value_or(0.0);
    if (const std::string *density = adjustment_dict->FindString("density")) {
      if (!DensityFromName(*density, &adjustment.density)) {
        return base::unexpected(SiteLayerError::kInvalidSelector);
      }
    }
    layer.adjustments.push_back(std::move(adjustment));
  }
  if (auto valid = ValidateSiteLayer(layer); !valid.has_value()) {
    return base::unexpected(valid.error());
  }
  return layer;
}

const char *SiteLayerErrorToString(SiteLayerError error) {
  switch (error) {
  case SiteLayerError::kInvalidId:
    return "invalid_id";
  case SiteLayerError::kInvalidName:
    return "invalid_name";
  case SiteLayerError::kInvalidOrigin:
    return "invalid_origin";
  case SiteLayerError::kEmptyLayer:
    return "empty_layer";
  case SiteLayerError::kTooManyRules:
    return "too_many_rules";
  case SiteLayerError::kInvalidSelector:
    return "invalid_selector";
  case SiteLayerError::kUnsafeSelector:
    return "unsafe_selector";
  case SiteLayerError::kInvalidColor:
    return "invalid_color";
  case SiteLayerError::kInvalidFontFamily:
    return "invalid_font_family";
  case SiteLayerError::kInvalidNumericValue:
    return "invalid_numeric_value";
  case SiteLayerError::kSelectorRequired:
    return "selector_required";
  case SiteLayerError::kSelectorNotAllowed:
    return "selector_not_allowed";
  case SiteLayerError::kUnsupportedSchema:
    return "unsupported_schema";
  case SiteLayerError::kUnknownLayer:
    return "unknown_layer";
  case SiteLayerError::kLimitExceeded:
    return "limit_exceeded";
  case SiteLayerError::kInUse:
    return "in_use";
  }
  return "invalid_selector";
}

} // namespace seoul
