#pragma once

#include <memory>

#include <ctl/static_array.h>

#include "modules/memory/column.h"

namespace celonis::accelerator::memory::builders {

/**
 * Creates temporary columns that are not registered in the memory manager.
 * Temporary columns can have a table as an owner, but the table doesn't know about the existence of the column.
 */
class temp_column_builder {
 public:
  temp_column_builder(col_name name, col_id id, std::optional<table_config> optional_table_config,
                      std::string cache_key)
      : name(std::move(name)),
        id(std::move(id)),
        optional_table_config_(std::move(optional_table_config)),
        cache_key(std::move(cache_key)) {}

  template <typename DATA_TYPE>
  column_t create_from_data(row_id row_count, ctl::static_array<DATA_TYPE> data, const null_flags_t& null_flags,
                            const column_processing_state& state) {
    std::string description{};
    std::shared_ptr<management::managed_memory_group> group{nullptr};

    if (optional_table_config_) {
      description = optional_table_config_->table_name() + "." + name.val;
      group = std::make_shared<management::managed_memory_group>("Column", optional_table_config_->table_id());
    }

    auto plain_data = materialized_typed_data<DATA_TYPE>::init_materialized_data(
        id.val, management::no_swap(), description, row_count, std::move(data), null_flags);

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
    return column_t{new column(std::move(config), optional_table_config_, nullptr, nullptr, std::move(plain_data),
                               column_loading::column_status::MATERIALIZED, std::move(group), state)};
  }

  column_t create_from_string_data(row_id row_count, ctl::static_array<cel_string_t> data, size_t str_bfr_size,
                                   ctl::static_array<char> string_bfr, const null_flags_t& null_flags,
                                   const column_processing_state& state);

 private:
  col_name name{""};
  col_id id{""};
  std::optional<table_config> optional_table_config_{std::nullopt};
  std::string cache_key;
};

}  // namespace celonis::accelerator::memory::builders
