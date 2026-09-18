// Project Seoul hybrid intelligence.

#include "seoul/browser/intelligence/streaming_accumulator.h"

#include <utility>

namespace seoul {

StreamingAccumulator::StreamingAccumulator(PayloadParser parser)
    : parser_(std::move(parser)) {}

StreamingAccumulator::~StreamingAccumulator() = default;

bool StreamingAccumulator::Feed(std::string_view chunk) {
  if (has_error()) return false;
  for (const SseEvent& event : sse_.Feed(chunk)) {
    if (event.done) {
      saw_stop_ = true;
      continue;
    }
    auto delta = parser_.Run(event.data);
    if (!delta.has_value()) {
      // SSE comments are handled by SseParser. A data event reporting an
      // error must never disappear merely because no text has arrived yet.
      error_ = delta.error();
      return false;
    }
    text_.append(delta->text);
    if (delta->input_tokens > 0) input_tokens_ = delta->input_tokens;
    if (delta->output_tokens > 0) output_tokens_ = delta->output_tokens;
    if (delta->stop) saw_stop_ = true;
  }
  if (sse_.overflowed()) {
    error_ = "response stream exceeded the maximum buffered size";
    return false;
  }
  return true;
}

GenerationResult StreamingAccumulator::Finish() {
  for (const SseEvent& event : sse_.Flush()) {
    if (event.done) {
      saw_stop_ = true;
      continue;
    }
    auto delta = parser_.Run(event.data);
    if (delta.has_value()) {
      text_.append(delta->text);
      if (delta->input_tokens > 0) input_tokens_ = delta->input_tokens;
      if (delta->output_tokens > 0) output_tokens_ = delta->output_tokens;
      if (delta->stop) saw_stop_ = true;
    } else {
      error_ = delta.error();
    }
  }
  GenerationResult result;
  result.text = text_;
  result.usage.input_tokens = input_tokens_;
  result.usage.output_tokens = output_tokens_;
  result.truncated = !saw_stop_ || has_error();
  return result;
}

}  // namespace seoul
