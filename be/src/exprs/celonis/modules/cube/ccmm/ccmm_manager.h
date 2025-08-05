#pragma once

#ifndef CELOSTAR
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/named_type.h"
#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/common/exceptions.h"
#include "modules/cube/ccmm/ccmm_manager_fwd.h"
#include "modules/cube/ccmm/types.h"
#include "modules/cube/cube_data_model_fwd.h"
#include "modules/cube/join_indexes.h"
#include "modules/cube/table_registry/derived_event_table_registry_fwd.h"
#include "modules/memory/cache/dimensional_join_cache_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/management/raw_data_handler_fwd.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table.h"
#endif

namespace celonis::accelerator::cube::ccmm {

// TODO(n.weber): CPL-10401 - This should only be 'ID' in the future
constexpr const char* EVENT_ID_COLUMN_KEY{"EVENT_ID"};
// TODO(n.weber): CPL-10401 - Deprecated for removal
constexpr const char* ACTIVITY_COLUMN_KEY{"ACTIVITY"};
// TODO(n.weber): CPL-10401 - This should only be 'TIME' in the future
constexpr const char* TIMESTAMP_COLUMN_KEY{"TIMESTAMP"};
// TODO(n.weber): CPL-10401 - This should only be 'ID' in the future
constexpr const char* OBJECT_ID_COLUMN_KEY{"OBJECT_ID"};
constexpr const char* SORTING_COLUMN_KEY{"SORTING"};
constexpr const char* SOURCE_COLUMN_KEY{"SOURCE_EVENT_TABLE"};

#ifndef CELOSTAR
/**
 * @brief Represents a user provided event table.
 */
class event_table {
 public:
  using maybe_column_t = std::optional<memory::column_t>;

  event_table() = delete;
  event_table(memory::table_t table, memory::column_t event_id_column, maybe_column_t maybe_activity_column,
              memory::column_t timestamp_column, memory::column_t sorting_column,
              const common::execution_context& context, std::optional<std::string> event_type_priority = std::nullopt);

  [[nodiscard]] memory::table_t get() const noexcept { return table_; }
  [[nodiscard]] const memory::table* get_raw() const noexcept { return table_.get(); }
  [[nodiscard]] const std::string& get_name() const noexcept { return table_->get_name(); }
  [[nodiscard]] memory::column_t get_event_id_column() const { return event_id_column_; }
  [[nodiscard]] memory::column_t get_activity_column() const;
  [[nodiscard]] memory::column_t get_timestamp_column() const { return timestamp_column_; }
  [[nodiscard]] memory::column_t get_sorting_column() const { return sorting_column_; }
  [[nodiscard]] const std::optional<std::string>& get_priority_string() const { return priority_string_; }
  [[nodiscard]] const memory::management::raw_data_handler_t<row_id>& get_sort_mapping() const {
    return row_idx_to_sort_idx_;
  }

 private:
  memory::table_t table_;
  memory::column_t event_id_column_;
  maybe_column_t maybe_activity_column_;
  memory::column_t timestamp_column_;
  memory::column_t sorting_column_;  // can be nullptr
  std::optional<std::string> priority_string_;

  // Unique mapping from a row in the event table to its position in the sorted sequence
  memory::management::raw_data_handler_t<row_id> row_idx_to_sort_idx_;
};
/**
 * @brief Represents a user provided object table.
 */
class object_table {
 public:
  object_table() = delete;
  object_table(memory::table_t table, memory::column_t object_id_column)
      : table_{std::move(table)}, object_id_column_{std::move(object_id_column)} {
    common::runtime_assert(table_ != nullptr && object_id_column_ != nullptr, "object_table inputs must not be null");
  }

  [[nodiscard]] memory::table_t get() const noexcept { return table_; }
  [[nodiscard]] const memory::table* get_raw() const noexcept { return table_.get(); }
  [[nodiscard]] const std::string& get_name() const noexcept { return table_->get_name(); }
  [[nodiscard]] memory::column_t get_object_id_column() const { return object_id_column_; }

 private:
  memory::table_t table_;
  memory::column_t object_id_column_;
};
/**
 * @brief Represents a user provided relationship table.
 *
 * N.B: This is different from the `mapping_table` class as it does not relate to the central events table.
 */
class relationship_table {
 public:
  relationship_table() = delete;
  explicit relationship_table(memory::table_t table) : table_{std::move(table)} {
    common::runtime_assert(table_ != nullptr, "relationship_table inputs must not be null");
  }

  [[nodiscard]] memory::table_t get() const noexcept { return table_; }
  [[nodiscard]] const memory::table* get_raw() const noexcept { return table_.get(); }
  [[nodiscard]] const std::string& get_name() const noexcept { return table_->get_name(); }

 private:
  memory::table_t table_;
};

/**
 * This class represents an instance of the Celonis Core Meta Model (CCMM).
 *
 * It is the superclass for ccmm_manager implementations. This uses an abstract base class to simplify
 * testing & mocking.
 */
// TODO(l.karnowski) A better name would be something like ccmm_instance
class ccmm_manager {
 public:
  using event_tables_t = std::vector<event_table>;
  using relationship_tables_t = std::vector<relationship_table>;
  using object_tables_t = std::vector<object_table>;
  using relationship_name_to_join_info_t = std::unordered_map<std::string, join_tables_info, common::ignore_case_hasher,
                                                              common::ignore_case_comparator_equal>;

  virtual ~ccmm_manager() = default;

  /** Returns whether the given table name (or pointer) refers to an object table in the CCMM */
  [[nodiscard]] virtual bool is_object_table(std::string_view table_name) const = 0;
  [[nodiscard]] bool is_object_table(memory::raw_table_ptr_t raw_table_ptr) const;
  /** Returns whether the given table name (or pointer) refers to an event table in the CCMM */
  [[nodiscard]] virtual bool is_event_table(std::string_view table_name) const = 0;
  [[nodiscard]] bool is_event_table(memory::raw_table_ptr_t raw_table_ptr) const;
  /** Returns whether the given table name (or pointer) refers to a relationship table in the CCMM */
  [[nodiscard]] virtual bool is_relationship_table(std::string_view table_name) const = 0;
  [[nodiscard]] bool is_relationship_table(memory::raw_table_ptr_t raw_table_ptr) const;

  /**
   * Returns all object tables in a vector
   */
  [[nodiscard]] virtual object_tables_t get_object_tables() const = 0;
  /**
   * Returns an object table by name. If the specified object table does not exist, returns an empty optional.
   */
  [[nodiscard]] virtual std::optional<object_table> get_object_table(std::string_view object_table_name) const = 0;
  /**
   * Returns an object table by name. If the specified object table does not exist, returns an empty optional.
   */
  [[nodiscard]] virtual std::optional<object_table> get_object_table(const object_name& object_table_name) const = 0;
  /**
   * @brief Returns an object table by name.
   * @throws common::internal_exception If the object table does not exist.
   */
  template <typename T>
  [[nodiscard]] object_table get_object_table_or_throw(const T& object_table_name) const {
    const auto optional_object_table{get_object_table(object_table_name)};
    common::runtime_assert(
        optional_object_table.has_value(),
        "No object table with name [{}] was found, are the object table name and object type name different?",
        object_table_name);
    return optional_object_table.value();
  }

  /**
   * Returns an event table by name. If the specified event table does not exist, returns an empty optional.
   */
  [[nodiscard]] virtual std::optional<event_table> get_event_table(std::string_view event_table_name) const = 0;
  /**
   * Returns an event table by name. If the specified event table does not exist, returns an empty optional.
   */
  [[nodiscard]] virtual std::optional<event_table> get_event_table(const activity_name& event_table_name) const = 0;
  /**
   * @brief Returns an event table by name.
   * @throws common::internal_exception If the event table does not exist.
   */
  template <typename T>
  [[nodiscard]] event_table get_event_table_or_throw(const T& event_table_name) const {
    const auto optional_event_table{get_event_table(event_table_name)};
    common::runtime_assert(
        optional_event_table.has_value(),
        "No event table with name [{}] was found, are the event table name and event type name different?",
        event_table_name);
    return optional_event_table.value();
  }
  /**
   * Returns all event tables in a vector.
   */
  [[nodiscard]] virtual const event_tables_t& get_event_tables() const = 0;

  /**
   * Returns a relationship table by name. If the specified relationship table does not exist, returns an empty
   * optional.
   */
  [[nodiscard]] virtual std::optional<relationship_table> get_relationship_table(
      std::string_view relationship_table_name) const = 0;

  [[nodiscard]] virtual std::optional<relationship_table> get_relationship_table(
      const relationship_name& relationship_table_name) const = 0;
  /**
   * @brief Returns a relationship table by name.
   * @throws common::internal_exception If the relationship table does not exist.
   */
  template <typename T>
  [[nodiscard]] relationship_table get_relationship_table_or_throw(const T& relationship_table_name) const {
    const auto optional_relationship_table{get_relationship_table(relationship_table_name)};
    common::runtime_assert(optional_relationship_table.has_value(), "No mapping table with name [{}] was found.",
                           relationship_table_name);
    return optional_relationship_table.value();
  }
  /**
   * Returns all relationship tables.
   */
  [[nodiscard]] virtual const relationship_tables_t& get_relationship_tables() const = 0;

  [[nodiscard]] virtual std::optional<join_tables_info> get_join_info_for_relationship_name(
      const std::string& relationship_name) const = 0;

 protected:
  ccmm_manager() = default;
  // These are protected to prevent slicing
  ccmm_manager(const ccmm_manager&) = default;
  ccmm_manager& operator=(const ccmm_manager&) = default;
  ccmm_manager(ccmm_manager&& other) noexcept = default;
  ccmm_manager& operator=(ccmm_manager&& other) noexcept = default;
};

/**
 * A real implementation of the ccmm_manager which holds the central event table and a mapping table for each object
 * to the central mapping table.
 */
class cube_ccmm_manager : public ccmm_manager {
 public:
  /**
   * Converts the given tables into the internal representation of the CCMM model.
   * 1. Merges the event_tables into a (sorted) central event table.
   * 2. Computes the mapping vectors between the object_tables and the central event table, by merging all the
   *    joins that are present in the relationship_tables.
   */
  [[nodiscard]] static std::unique_ptr<cube_ccmm_manager> create(
      cube_data_model& data_model, const memory::management::swap_info& sinfo, const event_tables_t& event_tables,
      const object_tables_t& object_tables, const relationship_tables_t& relationship_tables,
      relationship_name_to_join_info_t relationship_name_to_join_info, const common::execution_context& parent_context);

  ~cube_ccmm_manager() override = default;
  cube_ccmm_manager() = delete;
  cube_ccmm_manager(object_tables_t object_tables, event_tables_t event_tables,
                    relationship_tables_t relationship_tables,
                    relationship_name_to_join_info_t relationship_name_to_join_info);

  [[nodiscard]] bool is_object_table(std::string_view table_name) const final;
  [[nodiscard]] bool is_event_table(std::string_view table_name) const final;
  [[nodiscard]] bool is_relationship_table(std::string_view table_name) const final;
  [[nodiscard]] std::optional<object_table> get_object_table(std::string_view object_table_name) const final;
  [[nodiscard]] std::optional<object_table> get_object_table(const object_name& object_table_name) const final;
  [[nodiscard]] object_tables_t get_object_tables() const override;
  [[nodiscard]] std::optional<event_table> get_event_table(std::string_view event_table_name) const final;
  [[nodiscard]] std::optional<event_table> get_event_table(const activity_name& event_table_name) const final;
  [[nodiscard]] const event_tables_t& get_event_tables() const override;
  [[nodiscard]] std::optional<relationship_table> get_relationship_table(
      std::string_view relationship_table_name) const final;
  [[nodiscard]] std::optional<relationship_table> get_relationship_table(
      const relationship_name& relationship_table_name) const final;
  [[nodiscard]] const relationship_tables_t& get_relationship_tables() const override;
  [[nodiscard]] std::optional<join_tables_info> get_join_info_for_relationship_name(
      const std::string& relationship_name) const override {
    if (relationship_name_to_join_info_.contains(relationship_name)) {
      return relationship_name_to_join_info_.at(relationship_name);
    }
    return std::nullopt;
  }

 private:
  object_tables_t object_tables_;
  event_tables_t event_tables_;
  // TODO(j.kruska): Long term plan is to get rid of central event and internal mapping tables. Until then both mapping
  // table types exist side-by-side.
  relationship_tables_t user_provided_relationship_tables_;
  relationship_name_to_join_info_t relationship_name_to_join_info_{};
};
#endif

}  // namespace celonis::accelerator::cube::ccmm
