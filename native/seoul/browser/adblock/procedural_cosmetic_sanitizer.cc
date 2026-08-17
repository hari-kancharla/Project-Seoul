// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/adblock/procedural_cosmetic_sanitizer.h"

#include <optional>
#include <set>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace seoul::adblock {

SanitizedProceduralActionSets::SanitizedProceduralActionSets() = default;
SanitizedProceduralActionSets::SanitizedProceduralActionSets(
    const SanitizedProceduralActionSets&) = default;
SanitizedProceduralActionSets& SanitizedProceduralActionSets::operator=(
    const SanitizedProceduralActionSets&) = default;
SanitizedProceduralActionSets::SanitizedProceduralActionSets(
    SanitizedProceduralActionSets&&) = default;
SanitizedProceduralActionSets& SanitizedProceduralActionSets::operator=(
    SanitizedProceduralActionSets&&) = default;
SanitizedProceduralActionSets::~SanitizedProceduralActionSets() = default;

namespace {

constexpr size_t kMaxProceduralActions = 64;
constexpr size_t kMaxProceduralActionBytes = 4096;
constexpr size_t kMaxCombinedProceduralActionBytes = 128 * 1024;
constexpr size_t kMaxProceduralOperators = 8;
constexpr size_t kMaxProceduralSelectorBytes = 2048;
constexpr size_t kMaxProceduralTextBytes = 256;

struct ProceduralBudget {
  size_t count = 0;
  size_t bytes = 0;
  std::set<std::string> seen;
};

bool IsSafeSelector(std::string_view selector) {
  return !selector.empty() &&
         selector.size() <= kMaxProceduralSelectorBytes &&
         selector.front() != '@' &&
         selector.find('\0') == std::string_view::npos &&
         selector.find('{') == std::string_view::npos &&
         selector.find('}') == std::string_view::npos;
}

bool IsSafeIdentifier(std::string_view identifier, bool allow_colon_and_dot) {
  if (identifier.empty() || identifier.size() > 128) {
    return false;
  }
  for (const char character : identifier) {
    if (base::IsAsciiAlphaNumeric(character) || character == '-' ||
        character == '_' ||
        (allow_colon_and_dot && (character == ':' || character == '.'))) {
      continue;
    }
    return false;
  }
  return true;
}

std::optional<base::DictValue> SanitizeOperator(const base::Value& value) {
  const base::DictValue* source = value.GetIfDict();
  if (!source || source->size() != 2u) {
    return std::nullopt;
  }
  const std::string* type = source->FindString("type");
  const std::string* argument = source->FindString("arg");
  if (!type || !argument || argument->find('\0') != std::string::npos) {
    return std::nullopt;
  }

  bool valid = false;
  if (*type == "css-selector") {
    valid = IsSafeSelector(*argument);
  } else if (*type == "has-text") {
    // Regex input is deliberately deferred: accepting only literal text avoids
    // list-controlled catastrophic backtracking in the renderer.
    valid = !argument->empty() &&
            argument->size() <= kMaxProceduralTextBytes &&
            argument->front() != '/';
  } else if (*type == "min-text-length") {
    int minimum_length = 0;
    valid = base::StringToInt(*argument, &minimum_length) &&
            minimum_length >= 0 && minimum_length <= 100000;
  } else if (*type == "upward") {
    int levels = 0;
    valid = base::StringToInt(*argument, &levels) && levels >= 1 &&
            levels <= 16;
  }
  if (!valid) {
    return std::nullopt;
  }

  base::DictValue sanitized;
  sanitized.Set("type", *type);
  sanitized.Set("arg", *argument);
  return sanitized;
}

std::optional<base::DictValue> SanitizeAction(
    const base::DictValue& source) {
  const std::string* type = source.FindString("type");
  if (!type) {
    return std::nullopt;
  }
  base::DictValue sanitized;
  if (*type == "remove" && source.size() == 1u) {
    sanitized.Set("type", *type);
    return sanitized;
  }
  if (source.size() != 2u) {
    return std::nullopt;
  }
  const std::string* argument = source.FindString("arg");
  if (!argument) {
    return std::nullopt;
  }
  if (*type == "remove-attr" &&
      IsSafeIdentifier(*argument, /*allow_colon_and_dot=*/true)) {
    sanitized.Set("type", *type);
    sanitized.Set("arg", *argument);
    return sanitized;
  }
  if (*type == "remove-class" &&
      IsSafeIdentifier(*argument, /*allow_colon_and_dot=*/false)) {
    sanitized.Set("type", *type);
    sanitized.Set("arg", *argument);
    return sanitized;
  }
  return std::nullopt;
}

std::optional<std::string> SanitizeActionRecord(
    std::string_view serialized) {
  if (serialized.empty() || serialized.size() > kMaxProceduralActionBytes ||
      serialized.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(serialized)) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(serialized, /*options=*/0, /*max_depth=*/16);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& source = parsed->GetDict();
  const base::ListValue* operators = source.FindList("selector");
  const base::DictValue* action = source.FindDict("action");
  if (!operators || operators->empty() ||
      operators->size() > kMaxProceduralOperators ||
      source.size() != (action ? 2u : 1u)) {
    return std::nullopt;
  }

  base::ListValue sanitized_operators;
  for (const base::Value& value : *operators) {
    std::optional<base::DictValue> sanitized = SanitizeOperator(value);
    if (!sanitized) {
      return std::nullopt;
    }
    sanitized_operators.Append(std::move(*sanitized));
  }
  base::DictValue sanitized;
  sanitized.Set("selector", std::move(sanitized_operators));
  if (action) {
    std::optional<base::DictValue> sanitized_action = SanitizeAction(*action);
    if (!sanitized_action) {
      return std::nullopt;
    }
    sanitized.Set("action", std::move(*sanitized_action));
  }
  return base::WriteJson(sanitized);
}

std::vector<std::string> SanitizeActionList(
    const std::vector<std::string>& input,
    ProceduralBudget* budget) {
  std::vector<std::string> output;
  for (const std::string& serialized : input) {
    if (budget->count == kMaxProceduralActions) {
      break;
    }
    std::optional<std::string> sanitized = SanitizeActionRecord(serialized);
    if (!sanitized ||
        budget->bytes + sanitized->size() >
            kMaxCombinedProceduralActionBytes ||
        !budget->seen.insert(*sanitized).second) {
      continue;
    }
    ++budget->count;
    budget->bytes += sanitized->size();
    output.push_back(std::move(*sanitized));
  }
  return output;
}

}  // namespace

SanitizedProceduralActionSets SanitizeProceduralActionSets(
    const std::vector<std::string>& default_actions,
    const std::vector<std::string>& additional_actions) {
  ProceduralBudget budget;
  SanitizedProceduralActionSets result;
  result.default_actions = SanitizeActionList(default_actions, &budget);
  result.additional_actions = SanitizeActionList(additional_actions, &budget);
  return result;
}

}  // namespace seoul::adblock
