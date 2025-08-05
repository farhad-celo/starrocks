#pragma once

#include <memory>

#include "legacy_embedded_ctl/static_array.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/materialized_data_fwd.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::memory::builders {

/**
 * Creates temporary columns that are not registered in the memory manager.
 * Temporary columns can have a table as an owner, but the table doesn't know about the existence of the column.
 */
class temp_column_builder {
 public:
  temp_column_builder() = default;
  explicit temp_column_builder(table* owner, std::string cache_key) : owner(owner), cache_key(std::move(cache_key)) {}
  temp_column_builder(col_name name, col_id id, table* owner, std::string cache_key)
      : name(std::move(name)), id(std::move(id)), owner(owner), cache_key(std::move(cache_key)) {}

  template <typename DATA_TYPE>
  column_t create_from_data(row_id row_count, legacy_embedded_ctl::static_array<DATA_TYPE> data, const null_flags_t& null_flags,
                            const column_processing_state& state) {
    std::string description{};
    std::shared_ptr<management::managed_memory_group> group{nullptr};

    if (owner) {
      description = owner->get_name() + "." + name.val;
      group = std::make_shared<management::managed_memory_group>("Column", owner->get_id());
    }

    auto plain_data = materialized_typed_data<DATA_TYPE>::init_materialized_data(
        id.val, management::no_swap(), description, row_count, std::move(data), null_flags);

    column_ptrs_t column_pointers{nullptr};
    std::shared_ptr<dictionary> dict{nullptr};

    column_loading::column_config config;
    config.type = get_matching_data_type<DATA_TYPE>();
    config.cache_key = cache_key;
    config.name = name.val;
    config.id = id.val;
    config.swap_information = management::no_swap();
    config.row_count = row_count;
    config.description = description;

    // make_shared requires public constructor but constructors of column are private
    // NOLINTNEXTLINE(modernize-make-shared)
    return column_t{new column(std::move(config), owner, nullptr, std::move(column_pointers), std::move(dict),
                               std::move(plain_data), column_loading::column_status::MATERIALIZED, std::move(group),
                               state)};
  }

  column_t create_from_string_data(row_id row_count, legacy_embedded_ctl::static_array<cel_string_t> data, size_t str_bfr_size,
                                   legacy_embedded_ctl::static_array<char> string_bfr, const null_flags_t& null_flags,
                                   const column_processing_state& state);

  column_t create_from_materialized_data(row_id row_count, const materialized_data_t& plain_data,
                                         const column_processing_state& state);

  column_t create_from_dictionary(row_id row_count, const column_ptrs_t& column_pointers, const dictionary_t& dict,
                                  const column_processing_state& state);

  /**
   * Creates a temporary column which is based on the same dictionary and column configuration as the blueprint column.
   *
   * @param row_count number of rows required for the temporary column
   * @param blueprint dictionary and column configuration is used from this column
   * @param new_column_pointers column pointers for the temporary column
   * @param owner_after_pull_up owner after pull up if the column to create is the result of a pull-up
   * @return temporary column
   */
  column_t create_by_blueprint(row_id row_count, const column_t& blueprint,
                               const raw_column_ptrs_t& new_column_pointers, const memory::table* owner_after_pull_up,
                               const common::execution_context& context);

 private:
  col_name name{""};
  col_id id{""};
  table* owner{nullptr};
  std::string cache_key;
};

}  // namespace celonis::accelerator::memory::builders
