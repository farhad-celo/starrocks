#pragma once

#include <string>

#include "modules/common/tracing/span.h"

namespace celonis::accelerator::common {

/**
 * Contains general information about the execution of the current operation and enables tracing capabilities
 */
class execution_context {
 public:
  /**
   * This constructor creates a dummy context which manages a dummy span with no functionality
   */
  // Note: noexcept removed because std::make_shared can throw std::bad_alloc
  execution_context();

  /**
   * This constructor creates a context which manages a root span.
   * The creation of the root span results in a new empty trace.
   * The trace can be extended by creating a sub context or by adding related spans to the span of this context.
   * The trace ends when the lifetime of this context ends.
   *
   * @param operation_name name of work related to this context. Can be changed by changing the operation name of the
   * span.
   */
  // Note: noexcept removed because std::make_shared can throw std::bad_alloc
  explicit execution_context(const std::string& operation_name);

  // move-constructor only needed for creating sub context, custom implementation required because mutex is not movable
  execution_context(execution_context&& other_context) noexcept;
  /*
   * Don't allow move assignment because the underlying span time measurement shall only measure
   * the time of the scope where the execution_context was created.
   */
  execution_context& operator=(execution_context&& other_context) = delete;

  // not copyable as it would interfere with span time measurement
  execution_context& operator=(const execution_context&) = delete;

  /**
   * Creates a sub context. This results in the creation a child span of the span managed by this context.
   *
   * @param operation_name name of work related to the sub context. Can be changed by changing the operation name of the
   * span.
   * @param tags additional information about the work. Can also be added later to the span of the new context
   * @return the created sub context
   */
  // Note: noexcept removed because this calls a constructor that uses std::make_shared
  execution_context create_sub_context(const std::string& operation_name, const tracing::tags_t& tags) const;

  /**
   * @return span managed by this context
   */
  const tracing::span& get_span() const { return *span_; }
  tracing::span& get_span() { return *span_; }

  /**
   * @return shared_ptr to the span managed by this context. Only used for testing.
   */
  const tracing::span_t& get_shared_span() const { return span_; }

  ~execution_context();

 private:
  /**
   * Constructor for creating a sub context.
   *
   * @param managed_span span that is managed by the context
   * @param parent pointer to root execution_context
   */
  // Note: noexcept removed because std::make_shared can throw std::bad_alloc
  execution_context(tracing::span&& managed_span, const execution_context* parent);

  /**
   * Constructor for creating a sub context that shares the same span with the root context.
   *
   * @param parent pointer to root execution_context.
   */
  explicit execution_context(const execution_context* parent);

  const execution_context* parent_{nullptr};
  tracing::span_t span_;
};

}  // namespace celonis::accelerator::common
