#pragma once

#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>

namespace celonis::accelerator::common::tracing::propagation {
using datadog_id_t = uint64_t;
/**
 * Tag as defined in:
 * https://github.com/opentracing/specification/blob/master/semantic_conventions.md#span-tags-table
 * and
 * https://github.com/DataDog/opentracing-cpp/blob/v1.6.0/src/ext/tags.cpp#L14
 */
static constexpr const char* DATADOG_SAMPLING_PRIORITY_TAG{"sampling.priority"};
/**
 * Values taken from dd-opentracing-cpp
 * https://github.com/DataDog/dd-opentracing-cpp/blob/master/src/sampling_priority.h
 */
// Used for setting the sampling.priority tag which must be of type int according to specifications:
// https://github.com/opentracing/specification/blob/master/semantic_conventions.md#span-tags-table
static constexpr int DATADOG_USER_DROP_PRIORITY{-1};
// Used for sampling priority conversions from strings
static constexpr char DATADOG_SAMPLER_KEEP_PRIORITY{'1'};
// Used for sampling priority conversions from strings
static constexpr char DATADOG_USER_KEEP_PRIORITY{'2'};

opentelemetry::trace::TraceId to_opentelemetry_trace_id(opentelemetry::nostd::string_view datadog_id_as_string);
opentelemetry::trace::SpanId to_opentelemetry_span_id(opentelemetry::nostd::string_view datadog_id_as_string);
opentelemetry::trace::TraceFlags to_opentelemetry_trace_flags(
    opentelemetry::nostd::string_view datadog_sampling_priority);
}  // namespace celonis::accelerator::common::tracing::propagation
