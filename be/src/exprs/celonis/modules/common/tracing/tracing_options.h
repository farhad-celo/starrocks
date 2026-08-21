#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "log/metadata.h"
#include "modules/common/tracing/sampling/span_sampling_rules.h"

namespace celonis::accelerator::common::tracing {

enum class mode : std::int8_t { OFF, REMOTE };

// required for boost program options
std::istream& operator>>(std::istream& in, mode& trace_mode);
std::ostream& operator<<(std::ostream& os, const mode& trace_mode);

/** @see celonis::accelerator::common::tracing::remote_tracing_options::agent_host */
static constexpr const char* DEFAULT_DATADOG_AGENT_HOST{"localhost"};
/**
 * Default OTLP GRPC port according to specification:
 * https://github.com/open-telemetry/opentelemetry-specification/blob/HEAD/specification/protocol/otlp.md#otlpgrpc-default-port
 *
 * @see celonis::accelerator::common::tracing::remote_tracing_options::agent_port
 **/
constexpr std::uint32_t DEFAULT_DATADOG_AGENT_OTLP_GRPC_PORT{4317};
/** @see celonis::accelerator::common::tracing::remote_tracing_options::service */
static constexpr const char* DEFAULT_DATADOG_SERVICE_NAME{"compute"};
/** @see celonis::accelerator::common::tracing::remote_tracing_options::environment */
static constexpr const char* DEFAULT_DATADOG_ENVIRONMENT{"develop"};

/**
 * The name that identifies the Opentelemetry tracer for the query engine.
 **/
static constexpr const char* DEFAULT_OPENTELEMETRY_TRACER_LIBRARY_NAME{"cpm-accelerator-node"};

/**
 * @see celonis::accelerator::common::tracing::remote_tracing_options::sample_rate
 * @see celonis::accelerator::common::tracing::DEFAULT_DATADOG_APM_EVENT_SAMPLE_RATE
 */
static constexpr double DEFAULT_DATADOG_CLIENT_SAMPLE_RATE{1.0};

/**
 * @see celonis::accelerator::common::tracing::remote_tracing_options::span_sampling_rules
 */
inline auto default_span_sampling_rules() {
  static const sampling::span_sampling_rules_t DEFAULT_SPAN_SAMPLING_RULES{
      {log::to_span_name(log::request_type::DATA_MODEL_INFO), 0.01}};
  return DEFAULT_SPAN_SAMPLING_RULES;
}

/* @see celonis::accelerator::common::tracing::remote_tracing_options::shortest_recorded_span_duration */
static constexpr std::chrono::milliseconds DATADOG_SHORTEST_RECORDED_SPAN_DURATION{1};

/* @see celonis::accelerator::common::tracing::remote_tracing_options::service_type */
static constexpr const char* DEFAULT_DATADOG_SERVICE_TYPE{"custom"};

/* @see celonis::accelerator::common::tracing::remote_tracing_options::service_version */
static constexpr const char* DEFAULT_DATADOG_SERVICE_VERSION{""};

struct remote_tracing_options {
  /** Hostname or IP address of the Datadog trace agent */
  std::string agent_host{DEFAULT_DATADOG_AGENT_HOST};

  /** Port to which the traces are sent */
  std::uint32_t agent_port{DEFAULT_DATADOG_AGENT_OTLP_GRPC_PORT};

  /** Name of service being traced */
  std::string service{DEFAULT_DATADOG_SERVICE_NAME};

  /** Environment of service being traced */
  std::string environment{DEFAULT_DATADOG_ENVIRONMENT};

  /**
   * The client-side sampling rate specifies the percentage of traces that are sent to the agent, real number in [0, 1].
   * 0 = discard all traces, 1 = keep all traces.
   */
  double sample_rate{DEFAULT_DATADOG_CLIENT_SAMPLE_RATE};

  /**
   * Configuration of sampling rates for specific span names. This allows to reduce the amount of emitted traces for
   * specific spans.
   */
  sampling::span_sampling_rules_t span_sampling_rules{default_span_sampling_rules()};

  /**
   * Spans shorter than this duration are not recorded.
   * Added with CPL-8805 to reduce the amount of bytes send to Datadog because
   * the volume of ingested bytes for C++ tracing exceeded the free volume of 150GB per host.
   * https://docs.datadoghq.com/account_management/billing/apm_tracing_profiler/
   */
  std::chrono::milliseconds shortest_recorded_span_duration{DATADOG_SHORTEST_RECORDED_SPAN_DURATION};

  /**
   * Type of service being traced.
   */
  std::string service_type{DEFAULT_DATADOG_SERVICE_TYPE};

  /**
   * Version of service being traced.
   */
  std::string service_version{DEFAULT_DATADOG_SERVICE_VERSION};

  /**
   * A list of key/value pairs which will be set as a tag on all spans of each trace.
   * The attribute is initially empty and filled with the content of the DD_TAGS environment variable.
   */
  std::string tags{};
};

}  // namespace celonis::accelerator::common::tracing
