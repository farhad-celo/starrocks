#include "table.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <thread>
#include <utility>

#include <boost/algorithm/string/predicate.hpp>
#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/static_array.h"
#include "log/log.h"
#include "modules/common/call_and_log_unsafe_callable.h"
#ifndef CELOSTAR
#include "modules/io/swap/swap_loader.h"
#include "modules/memory/builders/cache_column_from_dictionary.h"
#include "modules/memory/cache/column_register.h"
#endif
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/null_flags.h"
#include "modules/memory/typed_dictionary.h"

namespace celonis::accelerator::memory {

namespace {

/**
 * @brief Verifies the table's meta data validity in relation to its swap info
 */
void verify_table_setting(const table& tbl) {
  const auto& tbl_meta_data{tbl.get_meta_data()};
  const bool is_no_swap{tbl.get_swap_info().is_no_swap()};
  const bool is_augmentation_or_query_scope_table{
      tbl_meta_data.is_augmentation_table() || tbl_meta_data.is_query_scope_table() ||
      tbl_meta_data.is_query_scope_aggregation_table() || tbl_meta_data.is_query_scope_result_table()};
  if (is_no_swap && !is_augmentation_or_query_scope_table) {
    log::error(
        "Invalid table setting for table [{}]. Swap info is 'no swap' but table type is not an augmentation- or query "
        "scope table and thus the table must be swappable.",
        tbl.get_name());
    throw common::internal_exception{"Invalid table setting for table [{}].", tbl.get_name()};
  }
  if (!is_no_swap && is_augmentation_or_query_scope_table) {
    log::error(
        "Invalid table setting for table [{}]. Swap info is not 'no swap' but table type is an augmentation- or query "
        "scope table which are not swappable.",
        tbl.get_name());
    throw common::internal_exception{"Invalid table setting for table [{}].", tbl.get_name()};
  }
}

void verify_row_limit(row_id row_count, const table_row_limit_t table_row_limit, const std::string& table_name) {
  if (row_count > 5'000'000'000) {
    log::jinfo("Very large table created", {{"row_count", row_count}});
  }
  if (!memory::check_row_limit(row_count, table_row_limit)) {
    throw common::internal_exception::with_context(
        {{"new_row_count", row_count}, {"row_limit", table_row_limit.get()}, {"table_name", table_name}},
        "Row count exceeds row limit.");
  }
}

}  // namespace

table::table(std::nullopt_t rows, std::string name, std::string id, const std::string& version,
             const management::swap_info& sinfo, const table_meta_data& meta_data)
    : default_row_count{rows},
      name{std::move(name)},
      id{std::move(id)},
      meta_data{meta_data},
#ifdef CELOSTAR
      sinfo{sinfo.swap_into_sub_dir(version + "/" + this->id)} {
#else
      sinfo{sinfo.swap_into_sub_dir(version + "/" + this->id)},
      cache{this, this->id, this->name, sinfo.swap_into_sub_dir(version + "/tmp/" + this->id)} {
#endif
  verify_table_setting(*this);
}

table::table(const row_id rows, std::string name, std::string id, const std::string& version,
             const management::swap_info& sinfo, const table_meta_data& meta_data,
             const table_row_limit_t table_row_limit)
    : table{std::nullopt, std::move(name), std::move(id), version, sinfo, meta_data} {
  verify_row_limit(rows, table_row_limit, get_name());
  default_row_count = rows;
}

table::table(std::nullopt_t rows, std::string name, std::string id, const management::swap_info& sinfo,
             const table_meta_data& meta_data, user_visible_table_name user_visible_name)
    : default_row_count{rows},
      name{std::move(name)},
      id{std::move(id)},
      user_visible_name{user_visible_name.get_name().empty() ? std::nullopt
                                                             : std::make_optional(std::move(user_visible_name))},
      meta_data{meta_data},
#ifdef CELOSTAR
      sinfo{sinfo.swap_into_sub_dir(this->id)} {
#else
      sinfo{sinfo.swap_into_sub_dir(this->id)},
      cache{this, this->id, this->name, sinfo.swap_into_sub_dir("/tmp/" + this->id)} {
#endif
  verify_table_setting(*this);
}

table::table(const row_id rows, std::string name, std::string id, const management::swap_info& sinfo,
             const table_meta_data& meta_data, user_visible_table_name user_visible_name,
             const table_row_limit_t table_row_limit)
    : table{std::nullopt, std::move(name), std::move(id), sinfo, meta_data, std::move(user_visible_name)} {
  verify_row_limit(rows, table_row_limit, get_name());
  default_row_count = rows;
}

namespace {
constexpr const char* QUERY_SCOPE_TABLE_VERSION{"0"};
}  // anonymous namespace

table::table(const row_id rows, const std::string& name, const table_row_limit_t table_row_limit)
    : table{std::nullopt, name} {
  verify_row_limit(rows, table_row_limit, get_name());
  default_row_count = rows;
}

table::table(const std::nullopt_t rows, const std::string& name)
    : table{rows,
            name,
            std::string{},
            QUERY_SCOPE_TABLE_VERSION,
            management::no_swap(),
            table_meta_data::make_for_query_scope_table()} {}

table::table(const row_id rows, const std::string& name, const table_meta_data meta_data,
             const table_row_limit_t table_row_limit)
    : table{std::nullopt, name, meta_data} {
  verify_row_limit(rows, table_row_limit, get_name());
  default_row_count = rows;
}

table::table(std::nullopt_t rows, const std::string& name, table_meta_data meta_data)
    : table{rows, name, std::string{}, QUERY_SCOPE_TABLE_VERSION, management::no_swap(), meta_data} {}

table::~table() {
  common::call_and_log_unsafe_callable(
      [this]() {
        if (remove_swap_files_on_destruct_) {
          for (auto& column : this->headers) {
            column->set_remove_swap_files_on_destruct();
          }
#ifndef CELOSTAR
          cache.set_remove_swap_files_on_destruct();
#endif
        }
      },
      "Failed to set remove_swap_files_on_destruct_flag");
}

std::string table::get_user_visible_name(const common::execution_context& context, bool bounds) const {
  if (auto visible_name{context.lookup_user_visible_name(this)}; visible_name.has_value()) {
    return bounds ? fmt::format(R"("{}")", visible_name->get_name()) : visible_name->get_name();
  }
  if (user_visible_name.has_value()) {
    return bounds ? fmt::format(R"(<{}>)", user_visible_name->get_name()) : user_visible_name->get_name();
  }
  return bounds ? fmt::format(R"("{}")", get_name()) : get_name();
}

column_t table::create_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                                  const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                                  const std::shared_ptr<dictionary>& dict,
                                                  const column_processing_state& processing_state) {
  const size_t row_count = column_pointers->get_row_count();
  if (row_count > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
    throw common::cpm_exception{"Size of column [{}] exceeds the limit of [{}]. Size of column [{}] is [{}].",
                                cache_key.val, std::numeric_limits<row_id>::max(), cache_key.val, row_count};
  }

  std::shared_ptr<materialized_data> plain_data(nullptr);

  std::shared_ptr<management::managed_memory_group> group =
      std::make_shared<management::managed_memory_group>("Column", get_id());

  column_loading::column_config config;
  config.type = type;
  config.name = column_name.val;
  config.id = column_id.val;
  config.cache_key = cache_key.val;
  config.row_count = static_cast<row_id>(row_count);
  config.description = create_column_description(column_name);
  config.swap_information = sinfo;

  column_t column_res(new column(config, this, nullptr, column_pointers, dict, std::move(plain_data),
                                 column_loading::column_status::DICTIFIED, group, processing_state));

  if (sinfo.memory_manager() != nullptr) {
    column_res->register_to_managed_group();
#ifndef CELOSTAR
    // TODO(j.kim): Revisit.
    sinfo.memory_manager()->register_persistent_group(group);
#endif
  }
  return column_res;
}

column_t table::create_column_with_data(data_type type, const col_name& column_name, const col_id& column_id,
                                        row_id row_count, const std::shared_ptr<materialized_data>& plain_data,
                                        const column_processing_state& state) {
  auto group = std::make_shared<management::managed_memory_group>("Column", get_id());
  management::swap_info swap_temp = sinfo;

  column_loading::column_config config;
  config.type = type;
  config.name = column_name.val;
  config.id = column_id.val;
  config.swap_information = std::move(swap_temp);
  config.row_count = row_count;
  config.description = create_column_description(column_name);

  column_t column_res(new column(config, this, nullptr, column_ptrs_t(nullptr), std::shared_ptr<dictionary>(nullptr),
                                 plain_data, column_loading::column_status::MATERIALIZED, group, state));

  if (sinfo.memory_manager() != nullptr) {
    column_res->register_to_managed_group();
#ifndef CELOSTAR
    sinfo.memory_manager()->register_persistent_group(group);
#endif
  }
  return column_res;
}

column_t table::create_empty_column(data_type type, const col_name& column_name, const col_id& column_id,
                                    const col_cache_key& cache_key, row_id row_count,
                                    const column_processing_state& state,
                                    const std::shared_ptr<column_loading::column_loader>& column_load) {
  column_ptrs_t column_pointers(nullptr);
  std::shared_ptr<dictionary> dict(nullptr);
  std::shared_ptr<materialized_data> plain_data(nullptr);

  std::shared_ptr<management::managed_memory_group> group =
      std::make_shared<management::managed_memory_group>("Column", get_id());

  management::swap_info swap_temp = sinfo;

  column_loading::column_config config;
  config.type = type;
  config.name = column_name.val;
  config.id = column_id.val;
  config.cache_key = cache_key.val;
  config.swap_information = std::move(swap_temp);
  config.row_count = row_count;
  config.description = create_column_description(column_name);

  column_t column_res(new column(std::move(config), this, nullptr, std::move(column_pointers), std::move(dict),
                                 std::move(plain_data), column_loading::column_status::MISSING, group, state,
                                 column_load));
#ifndef CELOSTAR
  if (sinfo.memory_manager() != nullptr) {
    sinfo.memory_manager()->register_persistent_group(group);
  }
#endif

  return column_res;
}

#ifndef CELOSTAR
column_t table::create_column_from_swap(data_type type, const col_name& column_name, const col_id& column_id,
                                        const column_processing_state& state) {
  std::shared_ptr<materialized_data> plain_data(nullptr);

  const std::string description = create_column_description(column_name);
  auto swap_data = io::swap::load_dict_column_from_swap(column_id.val, sinfo, description, type);
  if (!swap_data) {
    throw common::internal_exception{"Could not create column [{}] from swap.", description};
  }
  column_ptrs_t& column_pointers = swap_data->second;
  std::shared_ptr<dictionary>& dict = swap_data->first;

  return create_column_with_dictified_data(type, column_name, column_id, col_cache_key({}), column_pointers, dict,
                                           state);
}
#endif

std::vector<column_t> table::get_column_headers() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return std::vector<column_t>(std::cbegin(headers), std::cend(headers));
}

#ifndef CELOSTAR
void table::clear_cache() { cache.clear_cache(); }
#endif

bool table::has_column(const std::string_view column_name) const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return std::any_of(headers.cbegin(), headers.cend(),
                     [&column_name](const auto& header) { return boost::iequals(header->config_.name, column_name); });
}

bool table::is_constant_result() const { return false; }

table_t table::create_query_scope_table(const row_id rows, const std::string& name,
                                        const table_row_limit_t table_row_limit) {
  return std::make_shared<table>(rows, name, table_row_limit);
}

table_t table::create_query_scope_table(const std::nullopt_t rows, const std::string& name) {
  return std::make_shared<table>(rows, name);
}

column_t table::get_column_header(row_id column_index) const {
  std::shared_lock lck(table_mutex);
  if (static_cast<size_t>(column_index) >= headers.size()) {
    throw common::internal_exception{"Column index [{}] requested, only [{}] columns available.", column_index,
                                     headers.size()};
  }
  column_t col = headers[column_index];
  return col;
}

column_t table::get_column_header(const std::initializer_list<std::string_view>& column_names,
                                  const common::execution_context& context) const {
  const auto column_or_error_message{get_column_header_or_error_string(column_names, context)};
  if (std::holds_alternative<std::string>(column_or_error_message)) {
    throw common::cpm_exception{std::get<std::string>(column_or_error_message)};
  }

  legacy_embedded_debug_assert(std::holds_alternative<column_t>(column_or_error_message));
  return std::get<column_t>(column_or_error_message);
}

column_or_error_message_t table::get_column_header_or_error_string(
    const std::initializer_list<std::string_view>& column_names, const common::execution_context& context) const {
  const auto pred{[this](const std::string_view& sv) { return has_column(sv); }};
  const auto* const iter{std::find_if(column_names.begin(), column_names.end(), pred)};

  const auto concat_column_names{[&column_names]() {
    if (empty(column_names)) {
      return std::string{};
    }

    std::string msg{"\"" + std::string{*column_names.begin()} + "\""};
    for (const auto* it = std::next(column_names.begin()); it != column_names.end(); ++it) {
      msg.append(", \"" + std::string{*it} + "\"");
    }

    return msg;
  }};

  if (iter == column_names.end()) {
    return fmt::format("None of the column names in the list [{}] matches a column name in [\"{}\"].",
                       concat_column_names(), get_name());
  }

  if (!std::none_of(std::next(iter), column_names.end(), pred)) {
    return fmt::format("Multiple column names in the list [{}] match column names in [\"{}\"].", concat_column_names(),
                       get_name());
  }

  return get_column_header(*iter, context);
}

column_t table::get_column_header(const std::string_view column_name, const common::execution_context& context) const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  const auto variant{get_column_header_not_locked(column_name, context)};

  if (std::holds_alternative<std::string>(variant)) {
    throw common::cpm_exception{std::get<std::string>(variant)};
  }

  legacy_embedded_debug_assert(std::holds_alternative<column_t>(variant));
  return std::get<column_t>(variant);
}

column_or_error_message_t table::get_column_header_or_error_string(const std::string_view column_name,
                                                                   const common::execution_context& context) const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return get_column_header_not_locked(column_name, context);
}

cel_column_count_t table::get_columns() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return static_cast<cel_column_count_t>(headers.size());
}

std::optional<usage_time_t> table::time_of_last_usage() const {
  const std::shared_lock lck(table_mutex);
  std::optional<usage_time_t> last_column_usage{std::nullopt};
  std::ranges::for_each(headers, [&last_column_usage](const column_t& col) {
    // N.B: nullopt is less than anything and anything is greater than nullopt
    last_column_usage = std::max(last_column_usage, col->time_of_last_usage());
  });
  return last_column_usage;
}

bool table::is_row_count_set() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  auto rows{get_rows_if_known()};
  return rows.has_value();
}

row_id table::get_rows() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return get_rows_intern();
}

std::optional<row_id> table::get_rows_optional() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return get_rows_if_known();
}

bool table::is_query_scope() const noexcept {
  legacy_embedded_debug_assert(!get_meta_data().is_query_scope_table() ||
               get_swap_info().is_no_swap());  // query scope table => no swap
  return get_meta_data().is_query_scope_table() || get_meta_data().is_query_scope_result_table() ||
         get_meta_data().is_query_scope_aggregation_table();
}

void table::verify_set_row_count(const row_id new_row_count, std::optional<memory::table_row_limit_t> table_row_limit) {
  // Only other method that accesses default_row_count is private method get_rows_intern, which is only called
  // by shared_lock(table_mutex) protected get_rows
  std::unique_lock lck(table_mutex);
  verify_set_row_count_no_lock(new_row_count, table_row_limit);
}

column_t table::add_column_with_data(data_type type, const col_name& column_name, const col_id& column_id,
                                     row_id row_count, const std::shared_ptr<materialized_data>& plain_data,
                                     const column_processing_state& state, const table_row_limit_t table_row_limit) {
  return add_column(create_column_with_data(type, column_name, column_id, row_count, plain_data, state),
                    table_row_limit);
}

column_t table::add_empty_column(data_type type, const col_name& column_name, const col_id& column_id,
                                 const col_cache_key& cache_key, row_id row_count, const column_processing_state& state,
                                 const std::shared_ptr<column_loading::column_loader>& column_load,
                                 table_row_limit_t table_row_limit) {
  return add_column(create_empty_column(type, column_name, column_id, cache_key, row_count, state, column_load),
                    table_row_limit);
}

column_t table::add_empty_column(data_type type, const col_name& column_name, const col_id& column_id, row_id row_count,
                                 const column_processing_state& state,
                                 const std::shared_ptr<column_loading::column_loader>& column_load,
                                 const table_row_limit_t table_row_limit) {
  return add_empty_column(type, column_name, column_id, col_cache_key{""}, row_count, state, column_load,
                          table_row_limit);
}

column_t table::add_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                               const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                               const std::shared_ptr<dictionary>& dict,
                                               const table_row_limit_t table_row_limit) {
  return add_column(create_column_with_dictified_data(type, column_name, column_id, cache_key, column_pointers, dict,
                                                      column_processing_state()),
                    table_row_limit);
}

column_t table::add_column_with_dictified_data(const column_loading::column_config& col_config,
                                               const column_loading::dictified_column_data& dictified_data,
                                               const table_row_limit_t table_row_limit) {
  return add_column_with_dictified_data(col_config.type, memory::col_name{col_config.name},
                                        memory::col_id{col_config.id}, memory::col_cache_key{col_config.cache_key},
                                        dictified_data.col_ptrs, dictified_data.dict, table_row_limit);
}

column_t table::add_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                               const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                               const std::shared_ptr<dictionary>& dict,
                                               const column_processing_state& processing_state,
                                               const table_row_limit_t table_row_limit) {
  return add_column(create_column_with_dictified_data(type, column_name, column_id, cache_key, column_pointers, dict,
                                                      processing_state),
                    table_row_limit);
}

#ifndef CELOSTAR
column_t table::add_column_from_swap(data_type type, const col_name& column_name, const col_id& column_id,
                                     const column_processing_state& state, const table_row_limit_t table_row_limit) {
  return add_column(create_column_from_swap(type, column_name, column_id, state), table_row_limit);
}
#endif

void table::add_existing_column(const column_t& column, const table_row_limit_t table_row_limit) {
  const auto lck{concurrency::lock_validated(table_mutex, std::chrono::seconds{60})};
  if (std::cmp_greater_equal(headers.size(), std::numeric_limits<cel_column_count_t>::max())) {
    throw common::cpm_exception{"Maximum number of columns per table reached. Supported limit is [{}], but found [{}].",
                                std::to_string(std::numeric_limits<cel_column_count_t>::max()),
                                std::to_string(headers.size())};
  }
  if (!get_meta_data().is_augmentation_table()) {
    verify_set_row_count_no_lock(column->get_row_count(), table_row_limit);
  }
  headers.push_back(column);
}

column_t table::add_column_by_blueprint(const col_name& column_name, const col_cache_key& cache_key,
                                        const column_t& blueprint, const raw_column_ptrs_t& new_column_pointers,
                                        const column_processing_state& state, const table_row_limit_t table_row_limit,
                                        common::execution_context& context) {
  auto column_pointers =
      create_column_pointers(new_column_pointers, column_name.val, create_column_description(column_name), sinfo);
  return add_column_with_dictified_data(blueprint->get_data_type(), column_name, col_id(column_name.val), cache_key,
                                        column_pointers, blueprint->get_dict(context), state, table_row_limit);
}

column_t table::add_string_column(const col_name& column_name, const col_id& column_id,
                                  legacy_embedded_ctl::static_array<cel_string_t> ptrs, legacy_embedded_ctl::static_array<char> string_bfr,
                                  const null_flags_t& null_flags, const table_row_limit_t table_row_limit) {
  const auto row_count{ptrs.ssize()};
  const std::string description = create_column_description(column_name);
  std::shared_ptr<management::swappable_bitset> bitset(
      management::swappable_bitset::create_data_handler(null_flags, column_id.val + management::NULL_FLAGS_ENDING,
                                                        get_swap_info(), description + management::NULL_FLAGS_DESC));
  std::shared_ptr<management::string_data_handler> data_handler = management::string_data_handler::create_data_handler(
      std::move(ptrs), std::move(string_bfr), column_id.val,
      management::pointer_data_handler_swap_type::SWAPPED_MATERIALIZED, get_swap_info(), description);

  auto plain_data =
      std::make_shared<materialized_typed_data<cel_string_t>>(row_count, std::move(bitset), std::move(data_handler));
  return add_column_with_data(data_type::cel_string, column_name, column_id, row_count, std::move(plain_data),
                              column_processing_state(), table_row_limit);
}

column_t table::add_string_column(const col_name& column_name, const col_id& column_id,
                                  const optional_string_values_t& values, const table_row_limit_t table_row_limit,
                                  const common::execution_context& context) {
  row_id row_count{static_cast<row_id>(values.size())};
  std::unordered_map<std::string_view, size_t> distinct_value_to_offset_map{};
  size_t str_buffer_size{NULL_STRING.size()};

  // compute buffer size and buffer offsets
  for (const std::optional<std::string>& optional_value : values) {
    if (!optional_value.has_value()) {
      continue;
    }
    const auto& value{optional_value.value()};
    if (distinct_value_to_offset_map.contains(value)) {
      continue;
    }
    distinct_value_to_offset_map[value] = str_buffer_size;
    str_buffer_size += value.length() + 1;
  }

  auto column_values{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
      row_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  auto column_str_bfr{memory::tracking::make_static_array_for_overwrite<char>(
      str_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  auto null_flags{memory::create_null_flags(row_count, context)};

  // fill buffer
  std::copy_n(NULL_STRING.data(), NULL_STRING.size(), &column_str_bfr[0]);

  for (const auto& [value, offset] : distinct_value_to_offset_map) {
    std::copy_n(value.data(), value.length() + 1, &column_str_bfr[static_cast<std::ptrdiff_t>(offset)]);
  }

  // fill actual column with pointers to buffer
  for (row_id index{0}; index < row_count; index++) {
    const auto& optional_value{values.at(index)};
    if (!optional_value.has_value()) {
      column_values[index] = &column_str_bfr[0];
      null_flags->set(index);
      continue;
    }
    const auto& value{optional_value.value()};
    column_values[index] = &column_str_bfr[static_cast<ptrdiff_t>(distinct_value_to_offset_map[value])];
  }

  return add_string_column(column_name, column_id, std::move(column_values), std::move(column_str_bfr), null_flags,
                           table_row_limit);
}

void table::deregister() {
  for (auto& column : headers) {
    column->erase_column();
  }
#ifndef CELOSTAR
  cache.deregister_columns();
#endif
}

std::optional<row_id> table::get_rows_if_known() const {
  if (headers.empty()) {
    return default_row_count;
  }
  return headers[0]->get_row_count();
}

row_id table::get_rows_intern() const {
  auto rows{get_rows_if_known()};
  if (!rows.has_value()) {
    throw common::internal_exception::with_context({{"table_name", get_name()}}, "Row count unknown.");
  }
  return rows.value();
}

void table::verify_set_row_count_no_lock(const row_id new_row_count, std::optional<table_row_limit_t> table_row_limit) {
  // Only other method that accesses default_row_count is private method get_rows_intern, which is only called
  // by shared_lock(table_mutex) protected get_rows

  auto current_row_count{get_rows_if_known()};

  if (current_row_count.has_value()) {
    if (current_row_count.value() != new_row_count) {
      throw common::internal_exception::with_context({{"table_name", get_name()},
                                                      {"current_row_count", current_row_count.value()},
                                                      {"new_row_count", new_row_count}},
                                                     "Row count does not match.");
    }
  } else {
    if (!table_row_limit.has_value()) {
      log::warn(
          "The table {} is in an initial status that the default row count is unset. The new "
          "row count should be verified against the table row limit, but the row limit is not provided, which "
          "can lead to potential problems.",
          get_name());
    } else {
      verify_row_limit(new_row_count, table_row_limit.value(), get_name());
    }
  }

  if (!default_row_count.has_value()) {
    default_row_count = new_row_count;
  }
}

column_or_error_message_t table::get_column_header_not_locked(const std::string_view column_name,
                                                              const common::execution_context& context) const {
  for (const auto& header : headers) {
    if (boost::iequals(header->config_.name, column_name)) {
      return header;
    }
  }
  if (auto column{context.lookup_column_from_extended_table(this, column_name)}; column.has_value()) {
    return column.value();
  }
  return fmt::format("Column with name [\"{}\"] cannot be found on table [{}].", column_name,
                     get_user_visible_name(context));
}

column_t table::add_column(column_t&& column, const table_row_limit_t table_row_limit) {
  const auto lck{concurrency::lock_validated(table_mutex, std::chrono::seconds{60})};
  if (std::cmp_greater_equal(headers.size(), std::numeric_limits<cel_column_count_t>::max())) {
    throw common::cpm_exception{"Maximum number of columns per table reached. Supported limit is [{}], but found [{}].",
                                std::to_string(std::numeric_limits<cel_column_count_t>::max()),
                                std::to_string(headers.size())};
  }
  // TODO(d.grittner): due to the loading behavior of augmentation, we currently skip checking augmentation tables for
  // consistency.
  if (!get_meta_data().is_augmentation_table()) {
    // This will raise an exception if the row count does not match
    verify_set_row_count_no_lock(column->get_row_count(), table_row_limit);
  }
  headers.push_back(std::move(column));
  return headers.back();
}

[[nodiscard]] std::string table::create_column_description(const col_name& column_name) const {
  return fmt::format("{}.{}", get_name(), column_name.val);
}

void table::check_consistency_for_testing(const std::unordered_set<table*>& tables) const {
  for (const auto& col : headers) {
    col->check_consistency_for_testing(tables);
    common::runtime_assert(col->get_owner() == this, "Runtime Assertion failed");
  }

#ifndef CELOSTAR
  cache.check_consistency_for_testing(tables);
#endif
}
}  // namespace celonis::accelerator::memory
