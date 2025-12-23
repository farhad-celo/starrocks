#pragma once

#include <experimental/propagate_const>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/version.h>  // required for opentelemetry namespace

#include "legacy_embedded_ctl/checked_ptr.h"
#include "legacy_embedded_ctl/mutex.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace {
class Scope;
class Span;
}  // namespace trace
OPENTELEMETRY_END_NAMESPACE

namespace celonis::accelerator {
// forward declare for "modules/query/communication.pb.h"
class CommunicationRequest_ExecutionContext_SpanContext;
}  // namespace celonis::accelerator

namespace celonis::accelerator::common::tracing {

class span;

using span_t = legacy_embedded_ctl::checked_shared_ptr<span>;
using tag_t = opentelemetry::common::AttributeValue;
using tags_t = std::vector<std::pair<std::string, tag_t>>;
using opentelemetry_scope_t = std::experimental::propagate_const<std::unique_ptr<opentelemetry::trace::Scope>>;
using opentelemetry_span_t =
    std::experimental::propagate_const<opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>>;

/**
 * A span measures the time it takes to execute a logical unit of work.
 * The time is measured automatically from the time an instance of a span is created
 * till the same instance is destroyed.
 * Additional information about the work can be added as tags.
 *
 * span is an abstraction for the Span interface specified by OpenTracing. See:
 * - https://opentracing.io/specification/
 * - https://github.com/opentracing/opentracing-cpp
 */
class span final {
 public:
  /**
   * Creates a dummy span with no functionality
   */
  span() noexcept;

  /**
   * Creates a root span with no parent or other causal reference.
   * Creating a root span starts a new empty trace.
   *
   * @param operation_name name of work done by this span. Can also be changed later with change_operation_name
   * @param tags additional information about the work. Can also be added later with set_tag.
   */
  span(const std::string& operation_name, const tags_t& tags);

  /**
   * If a span context can be extracted from the remote span context this constructor creates a child span of
   * the remote span.
   *
   * If no span context can be extracted from the remote span context this constructor creates a root span.
   *
   * @param operation_name name of work done by this span. Can also be changed later with change_operation_name
   * @param tags additional information about the work. Can also be added later with set_tag.
   * @param remote_span_context context used for extracting a span context
   */
  span(const std::string& operation_name, const tags_t& tags,
       const CommunicationRequest_ExecutionContext_SpanContext& remote_span_context);

  ~span() noexcept;

  span(span&& other) noexcept;
  /*
   * Don't allow move assignment because the span shall only measure
   * the time of the scope where the span was created.
   */
  span& operator=(span&&) = delete;

  // not copyable as it would interfere with span time measurement
  span(const span&) = delete;
  span& operator=(const span&) = delete;

  // no heap allocation as late or no pointer deletion would interfere with span time measurement
  static void* operator new(size_t) = delete;
  static void* operator new[](size_t) = delete;

  /**
   * Change name of work done during this span.
   */
  void change_operation_name(const std::string& name);

  /**
   * Adds a tag to the span.
   * If there is a pre-existing value set for key, it is overwritten.
   */
  void set_tag(const std::string& key, const tag_t& value);

  /**
   * Adds a SQL like query to the span.
   * Datadog presents the query in dedicated display format.
   * If there is already a pre-existing query set, it is overwritten.
   */
  void set_query(const std::string& query);

  /**
   * Indicates that an error occurred during this span.
   * Datadog presents the error in dedicated display format.
   * @param message error message
   */
  void set_error(const std::string& message, const std::string& stack_trace = "");

  /**
   * Creates a span that is caused by this span (i.e., the parent span) and
   * this span depends on the work of the child span.
   * Often (but not always) the parent span cannot finish until the child span does.
   */
  [[nodiscard]] span start_child_span(const std::string& operation_name, const tags_t& tags) const;

  /**
   * This function indicates to the Datadog Agent to not send the span to Datadog.
   * This function must be called before the end of the span's lifetime or before a call to span::finish_span(),
   * otherwise it has no effect.
   */
  void drop_span();

  /**
   * Sets the end timestamp to the current time which is normally done when the lifetime of the span ends.
   * If finish is called a second time, it is guaranteed to do nothing.
   * When the lifetime of the span ends the end timestamp set by the first call of this function is used.
   *
   * Setting the end timestamp manually is useful if the lifetime of the span doesn't match with the
   * duration of the operation being measured by this span.
   */
  void finish_span();

 protected:
  explicit span(opentelemetry_span_t&& span);

 private:
  /**
   * The underlying Opentelemetry span which is managed by this class
   */
  legacy_embedded_ctl::owning_mutex<opentelemetry_span_t> span_impl_;
  /**
   * The underlying Opentelemetry scope that encapsulates the Opentelemetry span
   */
  opentelemetry_scope_t scope_impl_;
};

}  // namespace celonis::accelerator::common::tracing
