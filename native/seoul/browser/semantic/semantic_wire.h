// Project Seoul semantic data fabric.
// Canonical JSON wire codec for semantic results. The wire format is defined
// by protocol/semantic-result.schema.json (the cross-language contract shared
// with the TypeScript Canvas Design Lab); this codec is its C++
// implementation. Parse consumes an untrusted document and produces a typed
// SemanticResult or a precise violation; parsing is structural, so callers
// still run ValidateSemanticResult before trusting shape/role coherence.
// ParseSemanticResult(SemanticResultToValue(r)) reproduces `r`.

#ifndef SEOUL_BROWSER_SEMANTIC_SEMANTIC_WIRE_H_
#define SEOUL_BROWSER_SEMANTIC_SEMANTIC_WIRE_H_

#include "base/types/expected.h"
#include "base/values.h"
#include "seoul/browser/semantic/semantic_types.h"
#include "seoul/browser/semantic/semantic_validation.h"

namespace seoul {

// Wire-name mappings for the enums the semantic wire carries. Exposed so the
// protocol parity gate (scripts/check-protocol.mjs) and tests can hold them
// against the canonical schema.
const char* FieldPrimitiveToWire(FieldPrimitive primitive);
bool FieldPrimitiveFromWire(std::string_view s, FieldPrimitive* out);
const char* ValueClassToWire(ValueClass value_class);
bool ValueClassFromWire(std::string_view s, ValueClass* out);
const char* FieldSensitivityToWire(FieldSensitivity sensitivity);
bool FieldSensitivityFromWire(std::string_view s, FieldSensitivity* out);
const char* ResultStateToWire(ResultState state);
bool ResultStateFromWire(std::string_view s, ResultState* out);

// Canonical schema-only codec used by typed extraction capabilities. It shares
// the exact bounds and unknown-key rejection of SemanticResult parsing.
base::DictValue SemanticSchemaToValue(const SemanticSchema& schema);
base::expected<SemanticSchema, SemanticViolation> ParseSemanticSchema(
    const base::Value& value);

// Serializes one semantic result to the canonical wire document. Default
// field metadata (nullable=true, role=none, confidence=1.0, ...) is omitted;
// parsing restores it.
base::DictValue SemanticResultToValue(const SemanticResult& result);

// Parses one untrusted canonical wire document. Rejects unknown keys, an
// unsupported schema version, malformed field specs, and out-of-bounds
// collections. Structural only: run ValidateSemanticResult afterwards.
base::expected<SemanticResult, SemanticViolation> ParseSemanticResult(
    const base::Value& value);

}  // namespace seoul

#endif  // SEOUL_BROWSER_SEMANTIC_SEMANTIC_WIRE_H_
