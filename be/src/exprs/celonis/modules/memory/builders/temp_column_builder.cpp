#include "temp_column_builder.h"

#include "legacy_embedded_ctl/static_array.h"

namespace celonis::accelerator::memory::builders {

column_t temp_column_builder::create_from_string_data(row_id row_count, legacy_embedded_ctl::static_array<cel_string_t> data,
                                                      size_t str_bfr_size, legacy_embedded_ctl::static_array<char> string_bfr,
                                                      const null_flags_t& null_flags,
                                                      const column_processing_state& state) {
  std::string description{};
  std::shared_ptr<management::managed_memory_group> group{nullptr};

  if (owner != nullptr) {
    description = owner->get_name() + "." + name.val;
    group = std::make_shared<management::managed_memory_group>("Column", owner->get_id());
  }

  auto plain_data = materialized_typed_data<cel_string_t>::init_materialized_data(
      id.val, management::no_swap(), description, row_count, std::move(data), str_bfr_size, std::move(string_bfr),
      null_flags);

  column_ptrs_t column_pointers{nullptr};
  std::shared_ptr<dictionary> dict{nullptr};

  column_loading::column_config config;
  config.type = data_type::cel_string;
  config.cache_key = cache_key;
  config.name = name.val;
  config.id = id.val;
  config.swap_information = management::no_swap();
  config.row_count = row_count;
  config.description = std::move(description);

  return column_t(new column(std::move(config), owner, nullptr, std::move(column_pointers), std::move(dict),
                             std::move(plain_data), column_loading::column_status::MATERIALIZED, std::move(group),
                             state));
}

column_t temp_column_builder::create_from_materialized_data(row_id row_count,
                                                            const std::shared_ptr<materialized_data>& plain_data,
                                                            const column_processing_state& state) {
  std::string description{};
  std::shared_ptr<management::managed_memory_group> group{nullptr};

  if (owner != nullptr) {
    description = owner->get_name() + "." + name.val;
    group = std::make_shared<management::managed_memory_group>("Column", owner->get_id());
  }

  column_ptrs_t column_pointers{nullptr};
  std::shared_ptr<dictionary> dict{nullptr};

  column_loading::column_config config;
  config.type = plain_data->get_data_type();
  config.cache_key = cache_key;
  config.name = name.val;
  config.id = id.val;
  config.swap_information = management::no_swap();
  config.row_count = row_count;
  config.description = description;

  // make_shared requires public constructor but constructors of column are private
  // NOLINTNEXTLINE(modernize-make-shared)
  return column_t(new column(std::move(config), owner, nullptr, std::move(column_pointers), std::move(dict), plain_data,
                             column_loading::column_status::MATERIALIZED, std::move(group), state));
}

column_t temp_column_builder::create_from_dictionary(row_id row_count, const column_ptrs_t& column_pointers,
                                                     const dictionary_t& dict, const column_processing_state& state) {
  std::string description{};
  std::shared_ptr<management::managed_memory_group> group{nullptr};

  if (owner != nullptr) {
    description = owner->get_name() + "." + name.val;
    group = std::make_shared<management::managed_memory_group>("Column", owner->get_id());
  }

  std::shared_ptr<materialized_data> plain_data{nullptr};

  column_loading::column_config config;
  config.type = dict->type;
  config.cache_key = cache_key;
  config.name = name.val;
  config.id = id.val;
  config.swap_information = management::no_swap();
  config.row_count = row_count;
  config.description = std::move(description);

  return column_t(new column(std::move(config), owner, nullptr, column_pointers, dict, std::move(plain_data),
                             column_loading::column_status::DICTIFIED, std::move(group), state));
}

column_t temp_column_builder::create_by_blueprint(row_id row_count, const column_t& blueprint,
                                                  const raw_column_ptrs_t& new_column_pointers,
                                                  const memory::table* owner_after_pull_up,
                                                  const common::execution_context& context) {
  auto blueprint_context = context.create_sub_context(
      "create_blueprint", {{"column.name", blueprint->get_user_visible_name(context)}, {"column.rows", row_count}});

  std::shared_ptr<materialized_data> plain_data{nullptr};
  std::shared_ptr<management::managed_memory_group> group{nullptr};

  column_loading::column_config tmp_config = blueprint->config_;
  tmp_config.swap_information = management::no_swap();
  tmp_config.row_count = row_count;

  auto col_ptrs{create_tmp_column_pointers(new_column_pointers)};

  return column_t(new column(std::move(tmp_config), owner, owner_after_pull_up, std::move(col_ptrs),
                             blueprint->get_dict(blueprint_context), std::move(plain_data),
                             column_loading::column_status::DICTIFIED, std::move(group), blueprint->processing_state_));
}

}  // namespace celonis::accelerator::memory::builders
