#include "temp_column_builder.h"

namespace celonis::accelerator::memory::builders {

column_t temp_column_builder::create_from_string_data(row_id row_count, ctl::static_array<cel_string_t> data,
                                                      size_t str_bfr_size, ctl::static_array<char> string_bfr,
                                                      const null_flags_t& null_flags,
                                                      const column_processing_state& state) {
  std::string description{};
  std::shared_ptr<management::managed_memory_group> group{nullptr};

  if (optional_table_config_.has_value()) {
    description = optional_table_config_->table_name() + "." + name.val;
    group = std::make_shared<management::managed_memory_group>("Column", optional_table_config_->table_id());
  }

  auto plain_data = materialized_typed_data<cel_string_t>::init_materialized_data(
      id.val, description, row_count, std::move(data), str_bfr_size, std::move(string_bfr), null_flags);

  column_loading::column_config config;
  config.type = data_type::cel_string;
  config.cache_key = cache_key;
  config.name = name.val;
  config.id = id.val;
  config.row_count = row_count;
  config.description = std::move(description);

  return column_t(new column(std::move(config), optional_table_config_, nullptr, nullptr, std::move(plain_data),
                             column_loading::column_status::MATERIALIZED, std::move(group), state));
}

}  // namespace celonis::accelerator::memory::builders
