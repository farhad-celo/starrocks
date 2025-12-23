#pragma once

#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include "legacy_embedded_ctl/memory/memory_tracking_strategy.h"
#include "legacy_embedded_ctl/mutex.h"
#include "modules/common/tracing/span.h"
#include "modules/cube/extended_tables.h"
#include "modules/cube/table_to_user_visible_name_mapping.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/warnings.h"

#ifndef CELOSTAR
namespace celonis::accelerator {
// forward declare for "modules/query/communication.pb.h"
class CommunicationRequest_ExecutionContext;
}  // namespace celonis::accelerator
#endif

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
   * @param memory_tracking_strategy Strategy for memory tracking used in the scope defined by this execution context.
   * Will be inherited to all subcontexts.
   */
  // Note: noexcept removed because std::make_shared can throw std::bad_alloc
  explicit execution_context(const std::string& operation_name,
                             legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy = nullptr);

#ifndef CELOSTAR
  /**
   * If the remote context contains a valid span context this constructor creates a context which manages a sub span of
   * the remote span.
   *
   * If the span context is not valid or cannot be extracted from the remote context this constructor
   * creates a context which manages a root span. @see execution_context::execution_context(const std::string&)
   *
   * @param operation_name name of work related to this context.
   * @param remote_context information from this context is used when creating the execution_context
   * @param memory_tracking_strategy Strategy for memory tracking used in the scope defined by this execution context.
   * Will be inherited to all subcontexts.
   */
  execution_context(const std::string& operation_name, const CommunicationRequest_ExecutionContext& remote_context,
                    legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy = nullptr) noexcept;
#endif

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
   * Creates a sub context that references the same span as the current context.
   *
   * @return the created sub context
   */
  execution_context create_sub_context_with_same_span() const;

  /**
   * @return span managed by this context
   */
  const tracing::span& get_span() const { return *span_; }
  tracing::span& get_span() { return *span_; }

  /**
   * @return shared_ptr to the span managed by this context. Only used for testing.
   */
  const tracing::span_t& get_shared_span() const { return span_; }

  /**
   * Changes the memory tracking strategy that will be unsed in the scope of this execution context.
   * @param memory_tracking_strategy The new memory tracking strategy to be used
   */
  void set_memory_tracking_strategy(legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy);

  /**
   * @return A memory tracking strategy that should be used for the scope defined by this execution context. If no
   * strategy was set for this execution context, then this will return a nullptr.
   */
  [[nodiscard]] const legacy_embedded_ctl::abstract_strategy_t& get_memory_tracking_strategy() const;

  /**
   * Adds a warning to this context
   */
  void add_warning(const std::string& warning);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  void add_warning(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
    return add_warning(fmt::format(fmt, std::forward<ARGS>(args)...));
  }

  /**
   * Adds multiple warnings to this context
   */
  void add_warnings(const memory::warnings_container_t& warnings);
  void add_warnings(const memory::warnings_t& warnings);

  /**
   * @return a copy of all warnings that are currently stored for this context
   */
  memory::warnings_container_t get_warnings() const noexcept;

  void add_user_visible_name_mapping(const memory::table* table, memory::user_visible_table_name name);

  [[nodiscard]] std::optional<memory::user_visible_table_name> lookup_user_visible_name(
      const memory::table* table) const;

  void add_column_to_extended_table(const memory::table* table, const std::string& column_name,
                                    memory::column_t column);

  [[nodiscard]] std::optional<memory::column_t> lookup_column_from_extended_table(const memory::table* table,
                                                                                  std::string_view column_name) const;

  std::vector<std::string> get_columns_from_extended_table(const memory::table* table) const;

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

  void merge_warnings(memory::warnings_container_t warnings) const noexcept;

  const execution_context* parent_{nullptr};
  tracing::span_t span_;
  mutable memory::warnings_container_t warnings_;
  mutable std::shared_timed_mutex warnings_mutex_;
  legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy_{nullptr};
  legacy_embedded_ctl::owning_mutex<legacy_embedded_ctl::checked_shared_ptr<cube::table_to_user_visible_name_mapping>>
      table_to_user_visible_name_mapping_;
  legacy_embedded_ctl::owning_mutex<legacy_embedded_ctl::checked_shared_ptr<cube::extended_tables>> extended_tables_;
};

}  // namespace celonis::accelerator::common
