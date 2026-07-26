// Project Seoul semantic data fabric.

#include "seoul/browser/semantic/semantic_wire.h"

#include <cmath>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"

namespace seoul {

namespace {

// Wire bounds for the annotation lists, matching
// protocol/semantic-result.schema.json.
constexpr size_t kMaxWireErrors = 32;
constexpr size_t kMaxWireConflicts = 32;
constexpr size_t kMaxWireTransformations = 32;
constexpr size_t kMaxWireCitations = 64;

base::Time TimeFromMillis(double ms) {
  return base::Time::UnixEpoch() + base::Milliseconds(ms);
}

double MillisFromTime(base::Time t) {
  return (t - base::Time::UnixEpoch()).InMillisecondsF();
}

bool IsValidWireFieldId(std::string_view id) {
  if (id.empty() || id.size() > kMaxFieldIdLength) {
    return false;
  }
  if (id[0] < 'a' || id[0] > 'z') {
    return false;
  }
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

base::unexpected<SemanticViolation> WireErr(SemanticFabricError error,
                                            std::string detail) {
  SemanticViolation violation;
  violation.error = error;
  violation.detail = std::move(detail);
  return base::unexpected(std::move(violation));
}

// Rejects any key the canonical schema does not declare for `context`.
base::expected<void, SemanticViolation> RequireOnlyKeys(
    const base::DictValue& dict,
    base::span<const std::string_view> allowed,
    std::string_view context) {
  for (const auto [key, value] : dict) {
    bool known = false;
    for (std::string_view candidate : allowed) {
      if (key == candidate) {
        known = true;
        break;
      }
    }
    if (!known) {
      return WireErr(SemanticFabricError::kUnknownField,
                     std::string(context) + "." + key);
    }
  }
  return base::ok();
}

base::DictValue FieldSpecToValue(const FieldSpec& field) {
  base::DictValue dict;
  dict.Set("id", field.id);
  dict.Set("label", field.label);
  if (!field.description.empty()) {
    dict.Set("description", field.description);
  }
  dict.Set("primitive", FieldPrimitiveToWire(field.primitive));
  if (!field.nullable) {
    dict.Set("nullable", false);
  }
  if (field.role != SemanticRole::kNone) {
    dict.Set("role", SemanticRoleToString(field.role));
  }
  if (!field.unit.empty()) {
    dict.Set("unit", field.unit);
  }
  if (!field.currency_code.empty()) {
    dict.Set("currency_code", field.currency_code);
  }
  if (!field.locale.empty()) {
    dict.Set("locale", field.locale);
  }
  if (!field.timezone.empty()) {
    dict.Set("timezone", field.timezone);
  }
  if (!field.format.empty()) {
    dict.Set("format", field.format);
  }
  if (field.value_class != ValueClass::kContinuous) {
    dict.Set("value_class", ValueClassToWire(field.value_class));
  }
  if (field.confidence != 1.0) {
    dict.Set("confidence", field.confidence);
  }
  if (field.precision >= 0) {
    dict.Set("precision", field.precision);
  }
  if (field.sensitivity != FieldSensitivity::kPublic) {
    dict.Set("sensitivity", FieldSensitivityToWire(field.sensitivity));
  }
  if (!field.sortable) {
    dict.Set("sortable", false);
  }
  if (!field.filterable) {
    dict.Set("filterable", false);
  }
  if (field.aggregatable) {
    dict.Set("aggregatable", true);
  }
  return dict;
}

base::expected<FieldSpec, SemanticViolation> ParseFieldSpec(
    const base::DictValue& dict) {
  constexpr std::string_view kAllowed[] = {
      "id",       "label",     "description", "primitive", "nullable",
      "role",     "unit",      "currency_code", "locale",  "timezone",
      "format",   "value_class", "confidence", "precision", "sensitivity",
      "sortable", "filterable", "aggregatable"};
  if (auto only = RequireOnlyKeys(dict, kAllowed, "field"); !only.has_value()) {
    return base::unexpected(only.error());
  }
  FieldSpec field;
  const std::string* id = dict.FindString("id");
  if (!id || !IsValidWireFieldId(*id)) {
    return WireErr(SemanticFabricError::kInvalidFieldId, id ? *id : "(none)");
  }
  field.id = *id;
  const std::string* label = dict.FindString("label");
  if (!label || label->empty() || label->size() > kMaxFieldLabelLength) {
    return WireErr(SemanticFabricError::kInvalidLabel, field.id);
  }
  field.label = *label;
  if (const std::string* description = dict.FindString("description")) {
    field.description = *description;
  }
  const std::string* primitive = dict.FindString("primitive");
  if (!primitive || !FieldPrimitiveFromWire(*primitive, &field.primitive)) {
    return WireErr(SemanticFabricError::kFieldTypeMismatch,
                   field.id + ".primitive");
  }
  field.nullable = dict.FindBool("nullable").value_or(true);
  if (const std::string* role = dict.FindString("role")) {
    if (!SemanticRoleFromString(*role, &field.role)) {
      return WireErr(SemanticFabricError::kMissingRoleForShape,
                     field.id + ".role: " + *role);
    }
  }
  if (const std::string* unit = dict.FindString("unit")) {
    field.unit = *unit;
  }
  if (const std::string* currency = dict.FindString("currency_code")) {
    field.currency_code = *currency;
  }
  if (const std::string* locale = dict.FindString("locale")) {
    field.locale = *locale;
  }
  if (const std::string* timezone = dict.FindString("timezone")) {
    field.timezone = *timezone;
  }
  if (const std::string* format = dict.FindString("format")) {
    field.format = *format;
  }
  if (const std::string* value_class = dict.FindString("value_class")) {
    if (!ValueClassFromWire(*value_class, &field.value_class)) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     field.id + ".value_class");
    }
  }
  if (std::optional<double> confidence = dict.FindDouble("confidence")) {
    if (!std::isfinite(*confidence) || *confidence < 0.0 ||
        *confidence > 1.0) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     field.id + ".confidence");
    }
    field.confidence = *confidence;
  }
  if (std::optional<int> precision = dict.FindInt("precision")) {
    if (*precision < -1 || *precision > 12) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     field.id + ".precision");
    }
    field.precision = *precision;
  }
  if (const std::string* sensitivity = dict.FindString("sensitivity")) {
    if (!FieldSensitivityFromWire(*sensitivity, &field.sensitivity)) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     field.id + ".sensitivity");
    }
  }
  field.sortable = dict.FindBool("sortable").value_or(true);
  field.filterable = dict.FindBool("filterable").value_or(true);
  field.aggregatable = dict.FindBool("aggregatable").value_or(false);
  return field;
}

base::expected<std::vector<FieldSpec>, SemanticViolation> ParseFieldList(
    const base::ListValue& list,
    std::string_view context) {
  if (list.size() > kMaxSemanticFields) {
    return WireErr(SemanticFabricError::kTooManyFields, std::string(context));
  }
  std::vector<FieldSpec> fields;
  fields.reserve(list.size());
  for (const base::Value& value : list) {
    const base::DictValue* dict = value.GetIfDict();
    if (!dict) {
      return WireErr(SemanticFabricError::kInvalidShapeData,
                     std::string(context) + ": field spec must be an object");
    }
    auto field = ParseFieldSpec(*dict);
    if (!field.has_value()) {
      return base::unexpected(field.error());
    }
    fields.push_back(std::move(field.value()));
  }
  return fields;
}

base::DictValue SchemaToValue(const SemanticSchema& schema) {
  base::DictValue dict;
  dict.Set("schema_version", schema.schema_version);
  dict.Set("shape", SemanticShapeToString(schema.shape));
  if (!schema.fields.empty()) {
    base::ListValue fields;
    for (const FieldSpec& field : schema.fields) {
      fields.Append(FieldSpecToValue(field));
    }
    dict.Set("fields", std::move(fields));
  }
  if (!schema.edge_fields.empty()) {
    base::ListValue fields;
    for (const FieldSpec& field : schema.edge_fields) {
      fields.Append(FieldSpecToValue(field));
    }
    dict.Set("edge_fields", std::move(fields));
  }
  if (!schema.parts.empty()) {
    base::ListValue parts;
    for (const SemanticSchema& part : schema.parts) {
      parts.Append(SchemaToValue(part));
    }
    dict.Set("parts", std::move(parts));
    base::ListValue names;
    for (const std::string& name : schema.part_names) {
      names.Append(name);
    }
    dict.Set("part_names", std::move(names));
  }
  return dict;
}

base::expected<SemanticSchema, SemanticViolation> ParseSchema(
    const base::DictValue& dict,
    size_t depth) {
  constexpr std::string_view kAllowed[] = {"schema_version", "shape",
                                           "fields",         "edge_fields",
                                           "parts",          "part_names"};
  if (auto only = RequireOnlyKeys(dict, kAllowed, "schema");
      !only.has_value()) {
    return base::unexpected(only.error());
  }
  if (depth > kMaxCompositeDepth) {
    return WireErr(SemanticFabricError::kCompositeTooDeep, "");
  }
  SemanticSchema schema;
  std::optional<int> version = dict.FindInt("schema_version");
  if (!version.has_value() || *version != kSemanticSchemaVersion) {
    return WireErr(SemanticFabricError::kUnsupportedSchemaVersion,
                   version.has_value() ? base::NumberToString(*version)
                                       : "(missing)");
  }
  const std::string* shape = dict.FindString("shape");
  if (!shape || !SemanticShapeFromString(*shape, &schema.shape)) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   shape ? "unknown shape: " + *shape : "shape missing");
  }
  if (const base::ListValue* fields = dict.FindList("fields")) {
    auto parsed = ParseFieldList(*fields, "fields");
    if (!parsed.has_value()) {
      return base::unexpected(parsed.error());
    }
    schema.fields = std::move(parsed.value());
  }
  if (const base::ListValue* edges = dict.FindList("edge_fields")) {
    auto parsed = ParseFieldList(*edges, "edge_fields");
    if (!parsed.has_value()) {
      return base::unexpected(parsed.error());
    }
    schema.edge_fields = std::move(parsed.value());
  }
  const base::ListValue* parts = dict.FindList("parts");
  const base::ListValue* part_names = dict.FindList("part_names");
  if (parts || part_names) {
    if (!parts || !part_names || parts->size() != part_names->size()) {
      return WireErr(SemanticFabricError::kCompositePartMismatch, "");
    }
    if (parts->size() > kMaxCompositeParts) {
      return WireErr(SemanticFabricError::kCompositePartMismatch,
                     "too many parts");
    }
    for (const base::Value& part_value : *parts) {
      const base::DictValue* part_dict = part_value.GetIfDict();
      if (!part_dict) {
        return WireErr(SemanticFabricError::kCompositePartMismatch,
                       "part must be an object");
      }
      auto part = ParseSchema(*part_dict, depth + 1);
      if (!part.has_value()) {
        return base::unexpected(part.error());
      }
      schema.parts.push_back(std::move(part.value()));
    }
    for (const base::Value& name_value : *part_names) {
      const std::string* name = name_value.GetIfString();
      if (!name || !IsValidWireFieldId(*name)) {
        return WireErr(SemanticFabricError::kCompositePartMismatch,
                       "invalid part name");
      }
      schema.part_names.push_back(*name);
    }
  }
  return schema;
}

base::DictValue ProvenanceToValue(const SemanticProvenance& provenance) {
  base::DictValue dict;
  dict.Set("source_name", provenance.base.source_name);
  if (!provenance.base.source_url.empty()) {
    dict.Set("source_url", provenance.base.source_url);
  }
  dict.Set("retrieved_at_ms", MillisFromTime(provenance.base.retrieved_at));
  if (!provenance.base.effective_at.is_null()) {
    dict.Set("effective_at_ms", MillisFromTime(provenance.base.effective_at));
  }
  if (!provenance.base.timezone.empty()) {
    dict.Set("timezone", provenance.base.timezone);
  }
  if (!provenance.base.units.empty()) {
    dict.Set("units", provenance.base.units);
  }
  dict.Set("freshness", FreshnessStateToString(provenance.base.freshness));
  dict.Set("completeness", provenance.base.completeness);
  if (!provenance.provider.empty()) {
    dict.Set("provider", provenance.provider);
  }
  if (!provenance.transformations.empty()) {
    base::ListValue transformations;
    for (const std::string& transformation : provenance.transformations) {
      transformations.Append(transformation);
    }
    dict.Set("transformations", std::move(transformations));
  }
  if (!provenance.citations.empty()) {
    base::ListValue citations;
    for (const Citation& citation : provenance.citations) {
      base::DictValue citation_dict;
      citation_dict.Set("url", citation.url);
      if (!citation.title.empty()) {
        citation_dict.Set("title", citation.title);
      }
      citations.Append(std::move(citation_dict));
    }
    dict.Set("citations", std::move(citations));
  }
  return dict;
}

bool FreshnessFromWire(std::string_view s, FreshnessState* out) {
  if (s == "real_time") {
    *out = FreshnessState::kRealTime;
    return true;
  }
  if (s == "delayed") {
    *out = FreshnessState::kDelayed;
    return true;
  }
  if (s == "cached") {
    *out = FreshnessState::kCached;
    return true;
  }
  if (s == "stale") {
    *out = FreshnessState::kStale;
    return true;
  }
  return false;
}

base::expected<SemanticProvenance, SemanticViolation> ParseProvenance(
    const base::DictValue& dict) {
  constexpr std::string_view kAllowed[] = {
      "source_name", "source_url",     "retrieved_at_ms", "effective_at_ms",
      "timezone",    "units",          "freshness",       "completeness",
      "provider",    "transformations", "citations"};
  if (auto only = RequireOnlyKeys(dict, kAllowed, "provenance");
      !only.has_value()) {
    return base::unexpected(only.error());
  }
  SemanticProvenance provenance;
  const std::string* source_name = dict.FindString("source_name");
  if (!source_name || source_name->empty()) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   "provenance.source_name");
  }
  provenance.base.source_name = *source_name;
  if (const std::string* source_url = dict.FindString("source_url")) {
    provenance.base.source_url = *source_url;
  }
  std::optional<double> retrieved = dict.FindDouble("retrieved_at_ms");
  if (!retrieved.has_value() || !std::isfinite(*retrieved)) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   "provenance.retrieved_at_ms");
  }
  provenance.base.retrieved_at = TimeFromMillis(*retrieved);
  if (std::optional<double> effective = dict.FindDouble("effective_at_ms")) {
    if (!std::isfinite(*effective)) {
      return WireErr(SemanticFabricError::kInvalidShapeData,
                     "provenance.effective_at_ms");
    }
    provenance.base.effective_at = TimeFromMillis(*effective);
  }
  if (const std::string* timezone = dict.FindString("timezone")) {
    provenance.base.timezone = *timezone;
  }
  if (const std::string* units = dict.FindString("units")) {
    provenance.base.units = *units;
  }
  if (const std::string* freshness = dict.FindString("freshness")) {
    if (!FreshnessFromWire(*freshness, &provenance.base.freshness)) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     "provenance.freshness");
    }
  }
  if (std::optional<double> completeness = dict.FindDouble("completeness")) {
    if (!std::isfinite(*completeness) || *completeness < 0.0 ||
        *completeness > 1.0) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     "provenance.completeness");
    }
    provenance.base.completeness = *completeness;
  }
  if (const std::string* provider = dict.FindString("provider")) {
    provenance.provider = *provider;
  }
  if (const base::ListValue* transformations =
          dict.FindList("transformations")) {
    if (transformations->size() > kMaxWireTransformations) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     "provenance.transformations");
    }
    for (const base::Value& transformation : *transformations) {
      const std::string* text = transformation.GetIfString();
      if (!text) {
        return WireErr(SemanticFabricError::kInvalidShapeData,
                       "provenance.transformations");
      }
      provenance.transformations.push_back(*text);
    }
  }
  if (const base::ListValue* citations = dict.FindList("citations")) {
    if (citations->size() > kMaxWireCitations) {
      return WireErr(SemanticFabricError::kOutOfRangeValue,
                     "provenance.citations");
    }
    for (const base::Value& citation_value : *citations) {
      const base::DictValue* citation_dict = citation_value.GetIfDict();
      if (!citation_dict) {
        return WireErr(SemanticFabricError::kInvalidShapeData,
                       "provenance.citations");
      }
      constexpr std::string_view kCitationKeys[] = {"url", "title"};
      if (auto only =
              RequireOnlyKeys(*citation_dict, kCitationKeys, "citation");
          !only.has_value()) {
        return base::unexpected(only.error());
      }
      Citation citation;
      const std::string* url = citation_dict->FindString("url");
      if (!url || url->empty()) {
        return WireErr(SemanticFabricError::kInvalidShapeData, "citation.url");
      }
      citation.url = *url;
      if (const std::string* title = citation_dict->FindString("title")) {
        citation.title = *title;
      }
      provenance.citations.push_back(std::move(citation));
    }
  }
  return provenance;
}

}  // namespace

base::DictValue SemanticSchemaToValue(const SemanticSchema& schema) {
  return SchemaToValue(schema);
}

base::expected<SemanticSchema, SemanticViolation> ParseSemanticSchema(
    const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   "schema must be an object");
  }
  return ParseSchema(*dict, 0);
}

const char* FieldPrimitiveToWire(FieldPrimitive primitive) {
  switch (primitive) {
    case FieldPrimitive::kString:
      return "string";
    case FieldPrimitive::kInteger:
      return "integer";
    case FieldPrimitive::kNumber:
      return "number";
    case FieldPrimitive::kBoolean:
      return "boolean";
    case FieldPrimitive::kTimestamp:
      return "timestamp";
  }
  return "string";
}

bool FieldPrimitiveFromWire(std::string_view s, FieldPrimitive* out) {
  constexpr std::pair<std::string_view, FieldPrimitive> kNames[] = {
      {"string", FieldPrimitive::kString},
      {"integer", FieldPrimitive::kInteger},
      {"number", FieldPrimitive::kNumber},
      {"boolean", FieldPrimitive::kBoolean},
      {"timestamp", FieldPrimitive::kTimestamp}};
  for (const auto& [name, value] : kNames) {
    if (s == name) {
      *out = value;
      return true;
    }
  }
  return false;
}

const char* ValueClassToWire(ValueClass value_class) {
  switch (value_class) {
    case ValueClass::kCategorical:
      return "categorical";
    case ValueClass::kOrdinal:
      return "ordinal";
    case ValueClass::kContinuous:
      return "continuous";
    case ValueClass::kFreeText:
      return "free_text";
  }
  return "continuous";
}

bool ValueClassFromWire(std::string_view s, ValueClass* out) {
  constexpr std::pair<std::string_view, ValueClass> kNames[] = {
      {"categorical", ValueClass::kCategorical},
      {"ordinal", ValueClass::kOrdinal},
      {"continuous", ValueClass::kContinuous},
      {"free_text", ValueClass::kFreeText}};
  for (const auto& [name, value] : kNames) {
    if (s == name) {
      *out = value;
      return true;
    }
  }
  return false;
}

const char* FieldSensitivityToWire(FieldSensitivity sensitivity) {
  switch (sensitivity) {
    case FieldSensitivity::kPublic:
      return "public";
    case FieldSensitivity::kPersonal:
      return "personal";
    case FieldSensitivity::kSensitive:
      return "sensitive";
  }
  return "public";
}

bool FieldSensitivityFromWire(std::string_view s, FieldSensitivity* out) {
  constexpr std::pair<std::string_view, FieldSensitivity> kNames[] = {
      {"public", FieldSensitivity::kPublic},
      {"personal", FieldSensitivity::kPersonal},
      {"sensitive", FieldSensitivity::kSensitive}};
  for (const auto& [name, value] : kNames) {
    if (s == name) {
      *out = value;
      return true;
    }
  }
  return false;
}

const char* ResultStateToWire(ResultState state) {
  switch (state) {
    case ResultState::kComplete:
      return "complete";
    case ResultState::kPartial:
      return "partial";
    case ResultState::kStreaming:
      return "streaming";
    case ResultState::kFailed:
      return "failed";
  }
  return "complete";
}

bool ResultStateFromWire(std::string_view s, ResultState* out) {
  constexpr std::pair<std::string_view, ResultState> kNames[] = {
      {"complete", ResultState::kComplete},
      {"partial", ResultState::kPartial},
      {"streaming", ResultState::kStreaming},
      {"failed", ResultState::kFailed}};
  for (const auto& [name, value] : kNames) {
    if (s == name) {
      *out = value;
      return true;
    }
  }
  return false;
}

base::DictValue SemanticResultToValue(const SemanticResult& result) {
  base::DictValue dict;
  dict.Set("schema", SchemaToValue(result.schema));
  dict.Set("data", result.data.Clone());
  dict.Set("provenance", ProvenanceToValue(result.provenance));
  if (result.state != ResultState::kComplete) {
    dict.Set("state", ResultStateToWire(result.state));
  }
  if (!result.continuation_token.empty()) {
    dict.Set("continuation_token", result.continuation_token);
  }
  if (!result.unavailable_field_ids.empty()) {
    base::ListValue ids;
    for (const std::string& id : result.unavailable_field_ids) {
      ids.Append(id);
    }
    dict.Set("unavailable_field_ids", std::move(ids));
  }
  if (!result.errors.empty()) {
    base::ListValue errors;
    for (const SemanticError& error : result.errors) {
      base::DictValue error_dict;
      error_dict.Set("code", error.code);
      error_dict.Set("message", error.message);
      errors.Append(std::move(error_dict));
    }
    dict.Set("errors", std::move(errors));
  }
  if (!result.conflicts.empty()) {
    base::ListValue conflicts;
    for (const SourceConflict& conflict : result.conflicts) {
      base::DictValue conflict_dict;
      conflict_dict.Set("field_id", conflict.field_id);
      conflict_dict.Set("source_a", conflict.source_a);
      conflict_dict.Set("source_b", conflict.source_b);
      if (!conflict.note.empty()) {
        conflict_dict.Set("note", conflict.note);
      }
      conflicts.Append(std::move(conflict_dict));
    }
    dict.Set("conflicts", std::move(conflicts));
  }
  return dict;
}

base::expected<SemanticResult, SemanticViolation> ParseSemanticResult(
    const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   "result must be an object");
  }
  constexpr std::string_view kAllowed[] = {
      "schema",  "data",   "provenance", "state", "continuation_token",
      "unavailable_field_ids", "errors", "conflicts"};
  if (auto only = RequireOnlyKeys(*dict, kAllowed, "result");
      !only.has_value()) {
    return base::unexpected(only.error());
  }
  SemanticResult result;
  const base::DictValue* schema_dict = dict->FindDict("schema");
  if (!schema_dict) {
    return WireErr(SemanticFabricError::kEmptySchema, "schema missing");
  }
  auto schema = ParseSchema(*schema_dict, 0);
  if (!schema.has_value()) {
    return base::unexpected(schema.error());
  }
  result.schema = std::move(schema.value());
  const base::Value* data = dict->Find("data");
  if (!data) {
    return WireErr(SemanticFabricError::kInvalidShapeData, "data missing");
  }
  result.data = data->Clone();
  const base::DictValue* provenance_dict = dict->FindDict("provenance");
  if (!provenance_dict) {
    return WireErr(SemanticFabricError::kInvalidShapeData,
                   "provenance missing");
  }
  auto provenance = ParseProvenance(*provenance_dict);
  if (!provenance.has_value()) {
    return base::unexpected(provenance.error());
  }
  result.provenance = std::move(provenance.value());
  if (const std::string* state = dict->FindString("state")) {
    if (!ResultStateFromWire(*state, &result.state)) {
      return WireErr(SemanticFabricError::kOutOfRangeValue, "state");
    }
  }
  if (const std::string* token = dict->FindString("continuation_token")) {
    result.continuation_token = *token;
  }
  if (const base::ListValue* ids = dict->FindList("unavailable_field_ids")) {
    if (ids->size() > kMaxSemanticFields) {
      return WireErr(SemanticFabricError::kTooManyFields,
                     "unavailable_field_ids");
    }
    for (const base::Value& id_value : *ids) {
      const std::string* id = id_value.GetIfString();
      if (!id || !IsValidWireFieldId(*id)) {
        return WireErr(SemanticFabricError::kInvalidFieldId,
                       "unavailable_field_ids");
      }
      result.unavailable_field_ids.push_back(*id);
    }
  }
  if (const base::ListValue* errors = dict->FindList("errors")) {
    if (errors->size() > kMaxWireErrors) {
      return WireErr(SemanticFabricError::kOutOfRangeValue, "errors");
    }
    for (const base::Value& error_value : *errors) {
      const base::DictValue* error_dict = error_value.GetIfDict();
      if (!error_dict) {
        return WireErr(SemanticFabricError::kInvalidShapeData, "errors");
      }
      constexpr std::string_view kErrorKeys[] = {"code", "message"};
      if (auto only = RequireOnlyKeys(*error_dict, kErrorKeys, "error");
          !only.has_value()) {
        return base::unexpected(only.error());
      }
      const std::string* code = error_dict->FindString("code");
      const std::string* message = error_dict->FindString("message");
      if (!code || code->empty() || !message) {
        return WireErr(SemanticFabricError::kInvalidShapeData, "errors");
      }
      SemanticError error;
      error.code = *code;
      error.message = *message;
      result.errors.push_back(std::move(error));
    }
  }
  if (const base::ListValue* conflicts = dict->FindList("conflicts")) {
    if (conflicts->size() > kMaxWireConflicts) {
      return WireErr(SemanticFabricError::kOutOfRangeValue, "conflicts");
    }
    for (const base::Value& conflict_value : *conflicts) {
      const base::DictValue* conflict_dict = conflict_value.GetIfDict();
      if (!conflict_dict) {
        return WireErr(SemanticFabricError::kInvalidShapeData, "conflicts");
      }
      constexpr std::string_view kConflictKeys[] = {"field_id", "source_a",
                                                    "source_b", "note"};
      if (auto only =
              RequireOnlyKeys(*conflict_dict, kConflictKeys, "conflict");
          !only.has_value()) {
        return base::unexpected(only.error());
      }
      SourceConflict conflict;
      const std::string* field_id = conflict_dict->FindString("field_id");
      const std::string* source_a = conflict_dict->FindString("source_a");
      const std::string* source_b = conflict_dict->FindString("source_b");
      if (!field_id || !IsValidWireFieldId(*field_id) || !source_a ||
          source_a->empty() || !source_b || source_b->empty()) {
        return WireErr(SemanticFabricError::kInvalidShapeData, "conflicts");
      }
      conflict.field_id = *field_id;
      conflict.source_a = *source_a;
      conflict.source_b = *source_b;
      if (const std::string* note = conflict_dict->FindString("note")) {
        conflict.note = *note;
      }
      result.conflicts.push_back(std::move(conflict));
    }
  }
  return result;
}

}  // namespace seoul
