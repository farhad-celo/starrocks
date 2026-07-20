#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <utility>

#include "column_fwd.h"  // IWYU pragma: export
#include "legacy_embedded_ctl/assert.h"
#include "modules/common/execution_context.h"
#include "modules/memory/column_loading/column_loader.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/column_processing_state.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/typed_dictionary.h"
#include "modules/operators/framework/dictify_inputs.h"

namespace celonis::accelerator::memory {
namespace builders {
class temp_column_builder;
}  // namespace builders
}  // namespace celonis::accelerator::memory

namespace celonis::accelerator::memory {

/** Column ID strong type */
struct col_id {
  explicit col_id(std::string id) : val(std::move(id)) {}

  const std::string val;
};

/** Column name strong type */
struct col_name {
  explicit col_name(std::string name) : val(std::move(name)) {}

  const std::string val;
};

/** Column cache key strong type */
struct col_cache_key {
  explicit col_cache_key(std::string cache_key) : val(std::move(cache_key)) {}

  const std::string val;
};

/**
 * @brief The central data structure to represent columns in Saola DB.
 *
 * @description A column can be in one of three states:
 * 1. Missing - The column is not loaded from the source (i.e., parquet) yet.
 * 2. Materialized - The data is fully loaded and stored without any compression (e.g., dictionaries) in an array.
 * 3. Dictified - The data is fully loaded and dictionary compressed (to reduce the memory overhead).
 * If a column is accessed and in the state 'Missing' it is loaded from disk. Otherwise, or after the load, the column
 * is either materialized or dictified. When the dictionary of a materialized column is accessed, it will be dictified
 * implicitly and transparently for the caller.
 * @note The current column state can be obtained via methods provided in its interface. However, one can not guarantee
 * that this status doesn't change immediately. If an (operator) implementation requires a materialized column, you can
 * request the materialized data (method provided in the interface) and check that it is not null.
 * TODO(n.weber): This class desperately needs a clean up. Maybe feasible as part of delta loading. CPL-6234
 */
class column {
  friend class table;
  friend class builders::temp_column_builder;

  using no_dictify_request_t = std::optional<operators::no_dictify_request>;

 public:
  std::string get_string_value(row_id row, const common::execution_context& context = {});
  std::optional<std::string> get_string_value_opt(row_id row, const common::execution_context& context = {});

  /* Type information about the column */
  data_type get_data_type() const { return config_.type; }
  bool is_cel_string_type() const { return get_data_type() == data_type::cel_string; }
  bool is_cel_int_type() const { return get_data_type() == data_type::cel_int; }
  bool is_cel_float_type() const { return get_data_type() == data_type::cel_float; }
  bool is_cel_date_type() const { return get_data_type() == data_type::cel_date; }
  bool is_cel_boolean_type() const { return get_data_type() == data_type::cel_boolean; }

  row_id get_row_count(const common::execution_context& context = {});

  std::shared_ptr<dictionary> get_dict(const common::execution_context& context,
                                       const no_dictify_request_t& no_dictify_request = std::nullopt) {
    dictify_if_needed(context);
    return dict_;
  }

  /** Returns a raw pointer to the table which owns this column */
  table* get_owner() const { return owner_; }

  /* Trivial getters */
  const std::string& get_name() const noexcept { return config_.name; }
  const std::string& get_cache_key() const noexcept { return config_.cache_key; }
  std::string get_user_visible_name(const common::execution_context& context);
  std::string get_user_visible_owner_name(const common::execution_context& context) const;
  [[nodiscard]] const management::swap_info& get_swap_info() const noexcept { return config_.swap_information; }

  const column_processing_state& get_processing_state() const noexcept { return processing_state_; }

  std::shared_ptr<string_dictionary> get_string_dict(const common::execution_context& context,
                                                     const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_string) {
      throw common::internal_exception{"Column [{}] expected to be of type STRING, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<string_dictionary>(get_dict(context, no_dictify_request));
  }

  /** Returns the dictionary's size (i.e., the number of distinct values in this column) */
  row_id get_domain_count(const common::execution_context& context = {},
                          const no_dictify_request_t& no_dictify_request = std::nullopt);

  const column_ptrs_abstract& get_column_pointers(const common::execution_context& context,
                                                  const no_dictify_request_t& no_dictify_request = std::nullopt) {
    dictify_if_needed(context);
    column_pointers_->get_abstract()->swap_in(context);
    return *column_pointers_.get();
  }

  column_ptrs_t get_column_ptr_handle(const common::execution_context& context,
                                      const no_dictify_request_t& no_dictify_request = std::nullopt) {
    dictify_if_needed(context);
    return column_pointers_;
  }

  /**
   * Returns a nullptr if column is not materialized.
   * Do not rely on checking the status and expect the column to stay materialized.
   * Another thread can cause the column to become dictified.
   *
   * The correct way is to get the materialized data object and check if it is not null.
   */
  std::shared_ptr<materialized_data> get_materialized_data(const common::execution_context& context = {});

  /** Methods to request the status the column is currently in */
  column_loading::column_status get_column_status() const { return status_; }
  bool is_dictified() const { return get_column_status() == column_loading::column_status::DICTIFIED; }
  bool is_materialized() const { return get_column_status() == column_loading::column_status::MATERIALIZED; }

  column(column&&) = delete;
  column(const column&) = delete;
  column& operator=(column&&) = delete;
  column& operator=(const column&) = delete;

  ~column() = default;

 private:
  void dictify_if_needed(const common::execution_context& context);

  column(column_loading::column_config config, table* owner, const table* owner_after_pull_up,
         column_ptrs_t column_pointers, std::shared_ptr<dictionary> dict, std::shared_ptr<materialized_data> plain_data,
         column_loading::column_status status, std::shared_ptr<management::managed_memory_group> managed_group,
         column_processing_state processing_state = column_processing_state(),
         std::shared_ptr<column_loading::column_loader> column_load =
             std::shared_ptr<column_loading::column_loader>(nullptr))
      : config_(std::move(config)),
        owner_(owner),
        column_pointers_(std::move(column_pointers)),
        dict_(std::move(dict)),
        plain_data_(std::move(plain_data)),
        status_(status),
        managed_group_(std::move(managed_group)),
        processing_state_(std::move(processing_state)) {
    legacy_embedded_ctl::abort_assert(config_.row_count >= 0);
  }

  column_loading::column_config config_;
  table* owner_;
  // we store a reference to the owner - we need this in order to navigate in the object graph
  // the mutex is used for making swapping operations exclusive.
  // const row_id row_count;
  // data is either dictified or ...
  column_ptrs_t column_pointers_;
  std::shared_ptr<dictionary> dict_;
  // plain, but never both to save memory
  std::shared_ptr<materialized_data> plain_data_;

  std::atomic<column_loading::column_status> status_;

  std::shared_ptr<management::managed_memory_group> managed_group_;
  mutable std::shared_mutex column_mutex_;
  const column_processing_state processing_state_;

  static constexpr auto LOCK_LOGGING_THRESHOLD = std::chrono::seconds{1200};
};

}  // namespace celonis::accelerator::memory
