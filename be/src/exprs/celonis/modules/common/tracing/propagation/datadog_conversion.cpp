#include "datadog_conversion.h"

#ifndef CELOSTAR
#include <array>
#include <charconv>
#include <optional>

namespace celonis::accelerator::common::tracing::propagation {

std::optional<datadog_id_t> parse_datadog_id(std::string_view text) {
  static constexpr int DATADOG_ID_INTEGER_BASE{10};
  datadog_id_t result = 0;
  auto [ptr, error]{std::from_chars(text.data(), text.data() + text.size(), result, DATADOG_ID_INTEGER_BASE)};
  if (error == std::errc{}) {
    return result;
  }
  return std::nullopt;
}

opentelemetry::trace::TraceId to_opentelemetry_trace_id(opentelemetry::nostd::string_view datadog_id_as_string) {
  static_assert((2 * sizeof(datadog_id_t)) == opentelemetry::trace::TraceId::kSize);

  auto datadog_id{parse_datadog_id(datadog_id_as_string)};
  if (!datadog_id) {
    return {};
  }

  // Convert uint64_t to uint8_t[16] with bitwise operations instead of memcpy so we don't have to care about endianness
  // The Datadog ID is put into the last 8 bytes. The first 8 bytes are 0.
  std::array<uint8_t, opentelemetry::trace::TraceId::kSize> opentelemetry_trace_id{};
  for (size_t i = sizeof(datadog_id_t); i < opentelemetry_trace_id.size(); i++) {
    opentelemetry_trace_id[i] = uint8_t((*datadog_id >> 8 * (opentelemetry_trace_id.size() - 1 - i)) & 0xFF);
  }
  return opentelemetry::trace::TraceId{opentelemetry_trace_id};
}

opentelemetry::trace::SpanId to_opentelemetry_span_id(opentelemetry::nostd::string_view datadog_id_as_string) {
  static_assert(sizeof(datadog_id_t) == opentelemetry::trace::SpanId::kSize);

  auto datadog_id{parse_datadog_id(datadog_id_as_string)};
  if (!datadog_id) {
    return {};
  }

  // Convert uint64_t to uint8_t[8] with bitwise operations instead of memcpy so we don't have to care about endianness
  std::array<uint8_t, opentelemetry::trace::SpanId::kSize> opentelemetry_span_id{};
  for (size_t i = 0; i < opentelemetry_span_id.size(); i++) {
    opentelemetry_span_id[i] = uint8_t((*datadog_id >> 8 * (opentelemetry_span_id.size() - 1 - i)) & 0xFF);
  }
  return opentelemetry::trace::SpanId{opentelemetry_span_id};
}

opentelemetry::trace::TraceFlags to_opentelemetry_trace_flags(
    opentelemetry::nostd::string_view datadog_sampling_priority) {
  /*
   * The Datadog sampling priority is a single digit representing whether the trace shall be sampled.
   * 1 (SAMPLER_KEEP) and 2 (USER_KEEP) represent that the trace shall be sampled:
   * https://github.com/DataDog/dd-trace-java/blob/v0.88.0/dd-trace-api/src/main/java/datadog/trace/api/sampling/PrioritySampling.java
   */
  if (datadog_sampling_priority.length() == 1 && (datadog_sampling_priority[0] == DATADOG_SAMPLER_KEEP_PRIORITY ||
                                                  datadog_sampling_priority[0] == DATADOG_USER_KEEP_PRIORITY)) {
    return opentelemetry::trace::TraceFlags(opentelemetry::trace::TraceFlags::kIsSampled);
  }
  return opentelemetry::trace::TraceFlags(0);
}

}  // namespace celonis::accelerator::common::tracing::propagation
#endif