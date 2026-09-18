// Project Seoul hybrid intelligence.
// Shared streaming accumulation for provider adapters. Feeds transport bytes
// through the SSE parser, applies a per-event payload parser, and assembles the
// final GenerationResult (text, usage, truncation). Pure: no I/O, no provider
// specifics beyond the injected payload parser.

#ifndef SEOUL_BROWSER_INTELLIGENCE_STREAMING_ACCUMULATOR_H_
#define SEOUL_BROWSER_INTELLIGENCE_STREAMING_ACCUMULATOR_H_

#include <string>

#include "base/functional/callback.h"
#include "seoul/browser/intelligence/model_provider.h"
#include "seoul/browser/intelligence/provider_protocol.h"
#include "seoul/browser/intelligence/sse_parser.h"

namespace seoul {

// Parses one SSE data payload into a delta (chat_completions:: or messages::).
using PayloadParser =
    base::RepeatingCallback<base::expected<ProviderDelta, std::string>(
        const std::string& payload)>;

class StreamingAccumulator {
 public:
  explicit StreamingAccumulator(PayloadParser parser);
  ~StreamingAccumulator();

  // Feeds a raw transport chunk. Returns false and records an error if the
  // stream overflowed or a data payload was malformed. SSE comments are
  // handled by the parser and do not reach the payload decoder.
  bool Feed(std::string_view chunk);

  // Builds the final result from what was accumulated. `truncated` is set when
  // the stream ended without an explicit stop.
  GenerationResult Finish();

  bool saw_stop() const { return saw_stop_; }
  bool has_error() const { return !error_.empty(); }
  const std::string& error() const { return error_; }

 private:
  PayloadParser parser_;
  SseParser sse_;
  std::string text_;
  int input_tokens_ = 0;
  int output_tokens_ = 0;
  bool saw_stop_ = false;
  std::string error_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_INTELLIGENCE_STREAMING_ACCUMULATOR_H_
