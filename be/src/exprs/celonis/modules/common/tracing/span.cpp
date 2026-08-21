#include "span.h"

#include <thread>

#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>

#include "modules/common/tracing/propagation/datadog_conversion.h"
#include "modules/common/tracing/tracing_options.h"

namespace celonis::accelerator::common::tracing {

namespace {

constexpr opentelemetry::trace::SpanKind DEFAULT_SPAN_KIND{opentelemetry::trace::SpanKind::kInternal};

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer() noexcept {
  return opentelemetry::trace::Provider::GetTracerProvider()->GetTracer(
      tracing::DEFAULT_OPENTELEMETRY_TRACER_LIBRARY_NAME);
}

}  // namespace

span::span() noexcept = default;

span::span(const std::string& operation_name, const tags_t& tags)
    : span(get_tracer()->StartSpan(operation_name, tags)) {}

span::span(opentelemetry_span_t&& span)
    : span_impl_{std::move(span)}, scope_impl_{span_impl_.lock_mutable([](auto& span_impl) {
        return std::make_unique<opentelemetry::trace::Scope>(get_tracer()->WithActiveSpan(get_underlying(span_impl)));
      })} {}

span::~span() noexcept = default;

// Defined in source file because span_impl_ type is only complete here
span::span(span&& other) noexcept
    : span_impl_{other.span_impl_.lock_mutable([](auto& span_impl) { return std::move(span_impl); })},
      scope_impl_{std::move(other.scope_impl_)} {}

void span::change_operation_name(const std::string& name) {
  span_impl_.lock_mutable([&name = std::as_const(name)](auto& span_impl) {
    if (!span_impl) {
      return;
    }
    span_impl->UpdateName(name);
  });
}
void span::set_tag(const std::string& key, const tag_t& value) {
  span_impl_.lock_mutable([&key = std::as_const(key), &value = std::as_const(value)](auto& span_impl) {
    if (!span_impl) {
      return;
    }
    /*
     * Implement a defined behavior for nullptr values:
     * According to Opentelemetry specification attribute values of null are not valid and attempting to set a null
     * value is undefined behavior (https://opentelemetry.io/docs/reference/specification/common/). SetAttribute throws
     * an exception if a nullptr AttributeValue is passed. We detect this case here and replace the value with a text.
     */
    // Assert that opentelemetry::common::AttributeValue uses a c-string for storing nullptr values.
    static_assert(tag_t{nullptr}.index() == opentelemetry::common::kTypeCString);
    // This is due to the version mismatch of opentelemetry used by cpm and Celostar.
    if (const auto* cstring_value = opentelemetry::nostd::get_if<opentelemetry::common::kTypeCString>(&value);
        cstring_value != nullptr && *cstring_value == nullptr) {
      span_impl->SetAttribute(key, "nullptr");
    } else {
      span_impl->SetAttribute(key, value);
    }
  });
}

void span::set_query(const std::string& query) {
  span_impl_.lock_mutable([&query = std::as_const(query)](auto& span_impl) {
    if (!span_impl) {
      return;
    }
    /*
     * Using this attribute results in a dedicated display in a trace in Datadog.
     * See: https://docs.datadoghq.com/tracing/visualization/trace/?tab=spanmetadata#more-information
     */
    span_impl->SetAttribute("sql.query", query);
  });
}

void span::set_error(const std::string& message, const std::string& stack_trace) {
  span_impl_.lock_mutable(
      [&message = std::as_const(message), &stack_trace = std::as_const(stack_trace)](auto& span_impl) {
        if (!span_impl) {
          return;
        }
        /*
         * Using these attributes result in a dedicated display in a trace in Datadog.
         * See: https://docs.datadoghq.com/tracing/visualization/trace/?tab=spanmetadata#more-information
         */
        span_impl->SetStatus(opentelemetry::trace::StatusCode::kError);
        span_impl->SetAttribute("error.msg", message);
        span_impl->SetAttribute("error.type", "error");
        if (!stack_trace.empty()) {
          span_impl->SetAttribute("error.stack", stack_trace);
        }
      });
}

span span::start_child_span(const std::string& operation_name, const tags_t& tags) const {
  return span_impl_.lock_shared([&operation_name = std::as_const(operation_name),
                                 &tags = std::as_const(tags)](auto& span_impl) -> span {
    if (!span_impl) {
      // dummy spans have no span_impl_, so also return a dummy span
      return {};
    }
    return span{get_tracer()->StartSpan(
        operation_name, tags,
        opentelemetry::trace::StartSpanOptions{
            .start_system_time{}, .start_steady_time{}, .parent{span_impl->GetContext()}, .kind = DEFAULT_SPAN_KIND})};
  });
}

void span::drop_span() { set_tag(propagation::DATADOG_SAMPLING_PRIORITY_TAG, propagation::DATADOG_USER_DROP_PRIORITY); }

void span::finish_span() {
  span_impl_.lock_mutable([](auto& span_impl) {
    if (!span_impl) {
      return;
    }
    span_impl->End();
  });
}

}  // namespace celonis::accelerator::common::tracing
