#include "column.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>

#include <fmt/format.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/utility.h"
#include "log/log.h"
#include "modules/common/aligned_blocked_range.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/timer.h"
#ifndef CELOSTAR
#include "modules/cube/query_scope.h"
#include "modules/io/swap/swap_loader.h"
#endif
#include "modules/memory/builders/temp_column_builder.h"
#include "modules/memory/management/memory_manager.h"
#include "modules/memory/null_flags.h"
#include "modules/memory/table.h"
#include "modules/memory/transform/dictifier.h"

namespace celonis::accelerator::memory {

bool column::maybe_untyped_null_constant() { return is_constant() && this->is_cel_int_type() && has_domain_null(); }

bool column::has_dependency(const table* table) const {
  return std::find(dependencies_.cbegin(), dependencies_.cend(), table) != dependencies_.cend();
}

bool column::has_some_dependency(const std::unordered_set<memory::table*>& tables) const {
  return std::ranges::any_of(dependencies_,
                             [&tables](memory::table* dependency) { return tables.contains(dependency); });
}

void column::set_dependencies(const operators::operator_input_columns_t& input_columns) {
  for (const auto& column : input_columns) {
    if (column != nullptr) {
      if (column->get_owner() != nullptr) {
        dependencies_.emplace(column->get_owner());
      }

      const auto& column_dependencies{column->get_dependencies()};
      dependencies_.insert(column_dependencies.cbegin(), column_dependencies.cend());
    }
  }
}

std::string column::get_user_visible_name(const common::execution_context& context) {
  if (get_owner() == nullptr && get_row_count() == 1) {
    return "constant " + get_string_value(0);
  }
  if (!config_.cache_key.empty()) {
    return config_.cache_key;
  }
  return fmt::format(R"({}."{}")", get_user_visible_owner_name(context), get_name());
}

std::string column::get_user_visible_owner_name(const common::execution_context& context) const {
  if (owner_ == nullptr) {
    return "<no owner>";
  }
  return owner_->get_user_visible_name(context);
}

std::optional<std::string> column::get_string_value_opt(row_id row, const common::execution_context& context) {
  // Created to get a non-const context. Can be removed if context parameter becomes non-const
  auto get_value_context = context.create_sub_context("column::get_string_value_opt", {{"row", row}});
  const auto row_count = get_row_count(get_value_context);
  if (row < 0 || row >= row_count) {
    throw common::out_of_bounds_exception{"column::get_string_value", row_id{0}, (row_count - 1), row};
  }

  std::optional<std::string> value;

  auto materialized_data{get_materialized_data(get_value_context)};

  if (materialized_data != nullptr) {
    value = materialized_data->get_string_value_opt(row, get_value_context);
  } else {
    value = get_dict(get_value_context)->get_string_value_opt(get_column_pointers(get_value_context).get_ptr_slow(row));
  }
  return value;
}

std::string column::get_string_value(row_id row, const common::execution_context& context) {
  return get_string_value_opt(row, context).value_or("NULL");
}

row_id column::get_row_count(const common::execution_context& context) {
  if (config_.row_count < 0) {
    log::warn("Row count is {} for column {} - {}. ", config_.row_count, config_.name, config_.id);
  }
  // in case of augmentation tables, we might need to load the table first (to get the correct row count)
  if (belongs_to_augmentation_table() && is_missing()) {
    load_if_missing(context);
  }
  return config_.row_count;
}

bool column::is_scalar_value() {
  const auto* const owner{get_owner()};
  if (owner == nullptr) {
    return get_row_count() == 1;
  }
  return owner->is_constant_result();
}

void column::dictify_if_needed(const common::execution_context& context) {
  // #lizard forgives
  if (is_dictified()) {
    return;
  }

  load_if_missing(context);
  if (is_dictified()) {
    // the parquet load is triggered from load_column_if_missing. If the result is a dictified column, we have to stop
    // here
    return;
  }

  auto wait_span = context.get_span().start_child_span("dictify_wait_for_lock", {});
  const auto lck{concurrency::lock_with_logging(column_mutex_, LOCK_LOGGING_THRESHOLD)};
  wait_span.finish_span();

  auto dictify_context = context.create_sub_context("dictify_column", {});
  // check again after aquiring lock
  if (is_dictified()) {
    return;
  }

  const auto null_flags_bitset{plain_data_->get_null_flags()->get_const_data(dictify_context)};

  data_type type = config_.type;

  common::timer dictify_timer;
  const auto convert = [&](const auto& materialized_typed_data) {
    auto materialized_data = materialized_typed_data.get_const_data(dictify_context);
    // We start the timer after the data is loaded, as we do not want to include the time of swap ins
    dictify_timer.restart();
    auto raw = transform::dictify(std::span{materialized_data.get(), static_cast<size_t>(config_.row_count)},
                                  null_flags_bitset.get(), config_.description, dictify_context);
    auto [resulting_dict,
          resulting_column_pointers]{raw.to_swappable(config_.id, config_.swap_information, config_.description)};
#ifndef CELOSTAR
    if (config_.swap_information.is_persistent()) {
      const bool success{resulting_dict->write_out(dictify_context)};
      if (!success) {
        throw common::internal_exception{"Failed to write out newly created dictionary for column [{}].",
                                         get_user_visible_name(context)};
      }
      resulting_column_pointers->write_out(dictify_context);
    }
#endif
    dict_ = std::move(resulting_dict);
    column_pointers_ = std::move(resulting_column_pointers);
  };

  switch (type) {
    case data_type::cel_int:
      convert(dynamic_cast<materialized_typed_data<cel_int_t>&>(*plain_data_));
      break;
    case data_type::cel_float:
      convert(dynamic_cast<materialized_typed_data<cel_float_t>&>(*plain_data_));
      break;
    case data_type::cel_date:
      convert(dynamic_cast<materialized_typed_data<cel_date_t>&>(*plain_data_));
      break;
    case data_type::cel_string:
      convert(dynamic_cast<materialized_typed_data<cel_string_t>&>(*plain_data_));
      break;
    case data_type::cel_boolean:
      convert(dynamic_cast<materialized_typed_data<cel_boolean_t>&>(*plain_data_));
      break;
    case data_type::cel_uuid:
      convert(dynamic_cast<materialized_typed_data<cel_uuid_t>&>(*plain_data_));
      break;
    case data_type::cel_null:
      throw common::internal_exception("Dictification is not supported for cel_null_t");
  }
  if (managed_group_ != nullptr) {
    managed_group_->clear_group();
  }
  if (managed_group_ != nullptr) {
    managed_group_->add_to_group(column_pointers_->get_abstract());
    dict_->add_to_group(managed_group_);
  }
  status_ = column_loading::column_status::DICTIFIED;
  if (config_.swap_information.is_persistent()) {
    plain_data_->set_delete_from_disk_when_destructed(true);
  }
  plain_data_ = nullptr;

  dictify_timer.stop();

  if (auto* memory_manager = get_swap_info().memory_manager().get()) {
    memory_manager->add_invocation_to_operator_statistics(COLUMN_DICTIFY_KEY, dictify_timer.duration());
  }
}

void column::load_if_missing(const common::execution_context& context) {
  if (!is_missing()) {
    return;
  }

  auto wait_span{context.get_span().start_child_span("load_if_missing_wait_for_lock", {})};
  const auto lck{concurrency::lock_with_logging(column_mutex_, LOCK_LOGGING_THRESHOLD)};
  wait_span.finish_span();

#ifdef CELOSTAR
  legacy_embedded_debug_assert(!is_missing());
  return;
#else
  auto load_context{context.create_sub_context("load_if_missing", {})};

  if (is_missing()) {
    if (column_load_ != nullptr) {
      auto data{column_load_->load_missing_column(config_, load_context)};
      std::visit(
          legacy_embedded_ctl::overloaded{[this](std::shared_ptr<materialized_data>& mat_data) {
                                            if (belongs_to_augmentation_table()) {
                                              legacy_embedded_debug_assert(config_.row_count == 0);
                                              config_.row_count = mat_data->get_size();
                                            }
                                            plain_data_ = std::move(mat_data);
                                            legacy_embedded_debug_assert(plain_data_->get_size() == config_.row_count);
                                            status_ = column_loading::column_status::MATERIALIZED;
                                            column_pointers_ = nullptr;
                                            dict_ = nullptr;
                                          },
                                          [this](memory::column_loading::dictified_column_data& dic_data) {
                                            column_pointers_ = std::move(dic_data.col_ptrs);
                                            dict_ = std::move(dic_data.dict);
                                            legacy_embedded_debug_assert(column_pointers_->get_row_count() ==
                                                                         static_cast<size_t>(config_.row_count));
                                            status_ = column_loading::column_status::DICTIFIED;
                                            plain_data_ = nullptr;
                                          }},
          data);
    } else {
      throw common::internal_exception{"Load failed for column [{}].", get_user_visible_name(context)};
    }

    if (config_.swap_information.memory_manager() != nullptr) {
      register_to_managed_group(load_context);
      config_.swap_information.memory_manager()->register_persistent_group(managed_group_);
    }
  }
#endif
}

namespace {

struct exec_tmp_column_pointers {
  row_id domain_size_;
  bool has_null_;
  const common::execution_context& context;

  exec_tmp_column_pointers(const row_id domain_size, const bool has_null, const common::execution_context& context)
      : domain_size_{domain_size}, has_null_{has_null}, context{context} {}
  template <class COL_PTR_TYPE>
  column_ptrs_t operator()() {
    auto raw_column_pointers =
        create_raw_column_pointer<COL_PTR_TYPE>(domain_size_, memory::zero_init_t{false}, context);
    auto target_pointers = raw_column_pointers->get_data();
    std::iota(target_pointers.get(), std::next(target_pointers.get(), domain_size_),
              has_null_ ? COL_PTR_TYPE{0} : COL_PTR_TYPE{1});
    return create_tmp_column_pointers(raw_column_pointers);
  }
};

[[nodiscard]] column_ptrs_t make_tmp_column_pointers(const row_id domain_size_with_null, const row_id domain_size,
                                                     const bool has_null, const common::execution_context& context) {
  return memory::execute_with_column_pointers_type(exec_tmp_column_pointers(domain_size, has_null, context),
                                                   domain_size_with_null);
}

}  // namespace

column_t column::get_domain_column(common::execution_context& context) {
  const bool has_null{has_domain_null(context)};
  row_id domain_size_with_null = get_domain_count(context);
  row_id domain_size = domain_size_with_null - (has_null ? 0 : 1);

  const auto col_ptrs{make_tmp_column_pointers(domain_size_with_null, domain_size, has_null, context)};

  return builders::temp_column_builder(get_owner(), fmt::format("DOMAIN_COLUMN ( {} )", get_cache_key()))
      .create_from_dictionary(domain_size, col_ptrs, get_dict(context), memory::column_processing_state());
}

row_id column::get_domain_count(const common::execution_context& context,
                                const no_dictify_request_t& no_dictify_request) {
  check_implicit_dictification(no_dictify_request);
  dictify_if_needed(context);
  if (dict_ == nullptr) {
    return 0;
  }
  return dict_->get_size();
}

void column::swap_in(common::execution_context& context) {
  if (is_dictified()) {
    dict_->swap_in(context);
    column_pointers_->swap_in(context);
    return;
  }

  std::shared_lock lck(column_mutex_);

  if (is_dictified()) {
    dict_->swap_in(context);
    column_pointers_->swap_in(context);
  } else if (is_materialized()) {
    plain_data_->swap_in(context);
  }
}

#ifndef CELOSTAR
void column::swap_out(common::execution_context& context) {
  if (is_dictified()) {
    dict_->swap_out(context);
    column_pointers_->swap_out(context);
    return;
  }

  std::shared_lock lck(column_mutex_);

  if (is_dictified()) {
    dict_->swap_out(context);
    column_pointers_->swap_out(context);
  } else if (is_materialized()) {
    plain_data_->swap_out(context);
  }
}

bool column::write_out(common::execution_context& context) {
  if (is_dictified()) {
    const bool success = dict_->write_out(context);
    column_pointers_->write_out(context);
    return success;
  }

  std::shared_lock lck(column_mutex_);

  if (is_dictified()) {
    const bool success = dict_->write_out(context);
    column_pointers_->write_out(context);
    return success;
  }

  legacy_embedded_debug_assert(is_materialized());
  return plain_data_->write_out(context);
}

void column::swap_out_transaction(common::execution_context& context,
                                  std::chrono::steady_clock::time_point query_start) {
  managed_group_->swap_out(std::this_thread::get_id(), query_start, context);
}
#endif

bool column::is_at_least_partially_swapped_out() const {
  std::shared_lock lock{column_mutex_};
  if (plain_data_ != nullptr) {
    return plain_data_->get_load_status() == management::load_status::SWAPPED;
  }
  if (is_dictified()) {
    return column_pointers_->get_load_status() == management::load_status::SWAPPED ||
           dict_->get_load_status() == management::load_status::SWAPPED;
  }
  return false;
}

struct exec_project_null_flags {
  const row_id row_count;
  null_flags_bitset_t& null_flags;

  exec_project_null_flags(const row_id row_count,
                          null_flags_bitset_t& null_flags)  // NOLINT(google-runtime-references)
      : row_count(row_count), null_flags(null_flags) {}

  template <class TUPLE>
  void operator()(const TUPLE& t) {
    const auto col_ptrs_ac = std::get<0>(t).get_const_accessor();

    tbb::parallel_for(common::safe_aligned_blocked_range<row_id>{0, row_count}, [&](const auto r) {
      for (auto i = r.begin; i != r.end; ++i) {
        if (col_ptrs_ac[i] == 0) {
          null_flags.set(i);
        }
      }
    });
  }
};

void column::project_null_flags(null_flags_bitset_t& null_flags, const common::execution_context& context) {
  auto projection_context = context.create_sub_context("project_null_flags", {{"null_flags.size", null_flags.size()}});
  load_if_missing(context);

  std::shared_lock lck(column_mutex_);

  if (is_materialized()) {
    legacy_embedded_ctl::bitset_mutable_view view{null_flags};
    view |= plain_data_->get_null_flags()->get_const_data(projection_context).get();
    return;
  }
  const auto row_count = get_row_count();
  projection_context.get_span().set_tag("column.size", row_count);
  cast_execute_column_pointers(exec_project_null_flags(row_count, null_flags), get_column_pointers(projection_context));
}

struct exec_count_null_flags {
  const row_id column_size;

  template <typename TUPLE>
  row_id operator()(const TUPLE& t) {
    const auto column_ptr_ac = std::get<0>(t).get_const_accessor();

    tbb::enumerable_thread_specific<row_id> thread_local_null_count(0);
    tbb::parallel_for(tbb::blocked_range<row_id>{0, column_size, 1 << 18},
                      [&column_ptr_ac, &thread_local_null_count](const auto range) {
                        row_id& local_counter = thread_local_null_count.local();
                        for (row_id i = range.begin(); i < range.end(); i++) {
                          if (column_ptr_ac[i] == 0) {
                            local_counter++;
                          }
                        }
                      });

    return std::accumulate(thread_local_null_count.begin(), thread_local_null_count.end(), 0);
  }
};

row_id column::get_null_value_count(const common::execution_context& context) {
  const row_id row_count = get_row_count();

  row_id null_value_count{};
  if (auto copied_plain_data{get_materialized_data(context)}; copied_plain_data != nullptr) {
    const auto null_flags{copied_plain_data->get_null_flags()->get_const_data(context)};

    tbb::enumerable_thread_specific<row_id> local_null_count(0);
    tbb::parallel_for(common::safe_aligned_blocked_range<row_id>{0, row_count},
                      [&null_flags, &local_null_count](const auto range) {
                        row_id& local_counter = local_null_count.local();

                        for (row_id i = range.begin; i < range.end; i++) {
                          if (null_flags[i]) {
                            local_counter++;
                          }
                        }
                      });
    null_value_count = std::accumulate(local_null_count.begin(), local_null_count.end(), 0);

  } else {
    legacy_embedded_debug_assert(is_dictified());
    null_value_count = cast_execute_column_pointers(exec_count_null_flags{row_count}, *column_pointers_);
  }

  has_null_ = null_value_count > 0;
  return null_value_count;
}

std::shared_ptr<materialized_data> column::get_materialized_data(const common::execution_context& context) {
  load_if_missing(context);

  std::shared_lock lck(column_mutex_);

  if (!(is_materialized())) {
    return std::shared_ptr<materialized_data>();
  }
  return plain_data_;
}

std::optional<usage_time_t> column::time_of_last_usage() const {
  const std::shared_lock lck(column_mutex_);
  switch (get_column_status()) {
    case column_loading::MISSING:
      return std::nullopt;
    case column_loading::MATERIALIZED:
      return plain_data_->time_of_last_usage();
    case column_loading::DICTIFIED:
      return std::max(dict_->time_of_last_usage(), column_pointers_->time_of_last_usage());
  }
  legacy_embedded_ctl::assert_unreachable();
}

column_t column::alias_column(const std::string& alias, const std::string& cache_key, const std::string& format,
                              common::execution_context& context) {
  if (!processing_state_.is_aggregation_result()) {
    // TODO(l.karnowski) Conceptually, alias should not dictify the original column. The reason why it currently needs
    // to dictify is because the alias_operator_node is compiled *before* aggregations/filter_sort in the Java part.
    // Since alias_column() creates a copy of the column which is then the input for aggregations/filter_sort - and
    // those operators currently require dictified input in most cases - dictify is triggered in here to avoid
    // having to compute the dictified column twice: Once for the cloned alias column and once for the original
    // column which could e.g. reside in a cache. CPL-8699
    check_implicit_dictification(operators::no_dictify_request{});
    dictify_if_needed(context);
  }

  std::shared_lock lck(column_mutex_);

  column_loading::column_config alias_config = config_;
  alias_config.swap_information = management::no_swap();
  alias_config.name = alias;
  alias_config.id = config_.id + std::string("_ALIAS_") + alias;
  alias_config.cache_key = cache_key;

  column_processing_state alias_state{processing_state_};
  alias_state.set_format(format);

  std::string owner_id{};
  if (owner_ != nullptr) {
    owner_id = owner_->get_id();
  }

  column_t aliased_column(new column(alias_config, owner_, nullptr, column_pointers_, dict_, plain_data_, status_,
                                     std::make_shared<management::managed_memory_group>("Column", owner_id),
                                     alias_state));
  return aliased_column;
}

struct exec_has_domain_null {
  const row_id row_count;

  explicit exec_has_domain_null(const row_id row_count) : row_count(row_count) {}

  template <class TUPLE>
  bool operator()(const TUPLE& t) {
    const auto col_ptrs_ac = std::get<0>(t).get_const_accessor();

    return tbb::parallel_reduce(
        tbb::blocked_range<row_id>{0, row_count, 100000}, false,
        [&col_ptrs_ac](const auto r, bool init) -> bool {
          for (auto a = r.begin(); a < r.end() && !init; ++a) {
            init = init || col_ptrs_ac[a] == 0;
          }
          return init;
        },
        std::logical_or<>());
  }
};

bool column::has_domain_null(const common::execution_context& context) {
  if (auto has_null{has_null_.load()}; has_null.has_value()) {
    return has_null.value();
  }

  load_if_missing(context);

  std::shared_lock lck(column_mutex_);

  if (auto has_null{has_null_.load()}; has_null.has_value()) {
    return has_null.value();
  }

  if (is_dictified()) {
    has_null_ = cast_execute_column_pointers(exec_has_domain_null(get_row_count()), *column_pointers_);
  } else if (is_materialized()) {
    has_null_ = plain_data_->get_null_flags()->get_const_data(context).any();
  }
  legacy_embedded_debug_assert(has_null_.load().has_value());
  return has_null_.load().value();
}

column_ptrs_t column::set_and_register_column_pointer(const column_ptrs_t& new_column_pointers,
                                                      const common::execution_context& context) {
  if (!is_dictified()) {
    throw common::internal_exception{"Update column pointer for column [{}] failed because column is not dictified.",
                                     get_user_visible_name(context)};
  }

  const auto lck{concurrency::lock_with_logging(column_mutex_, LOCK_LOGGING_THRESHOLD)};

  if (managed_group_ != nullptr) {
    managed_group_->clear_group();
  }
  auto old_column_pointers{column_pointers_};
  column_pointers_ = new_column_pointers;
  if (managed_group_ != nullptr) {
    managed_group_->add_to_group(column_pointers_->get_abstract());
    dict_->add_to_group(managed_group_);
  }
  return old_column_pointers;
}

std::pair<column_ptrs_t, std::shared_ptr<dictionary>> column::set_and_register_dictified_data(
    const column_ptrs_t& new_column_pointers, const std::shared_ptr<dictionary>& new_dictionary,
    const common::execution_context& context) {
  if (!is_dictified()) {
    throw common::internal_exception{"Update column pointer for column [{}] failed because column is not dictified.",
                                     get_user_visible_name(context)};
  }

  const auto lck{concurrency::lock_with_logging(column_mutex_, LOCK_LOGGING_THRESHOLD)};

  if (managed_group_ != nullptr) {
    managed_group_->clear_group();
  }
  auto old_column_pointers{column_pointers_};
  auto old_dict{dict_};
  column_pointers_ = new_column_pointers;
  dict_ = new_dictionary;
  if (managed_group_ != nullptr) {
    managed_group_->add_to_group(column_pointers_->get_abstract());
    dict_->add_to_group(managed_group_);
  }
  return {std::move(old_column_pointers), std::move(old_dict)};
}

// Note: this method is not protected by locks and needs to be called in a scope in which column_mutex is locked
void column::register_to_managed_group(const common::execution_context& context) {
  load_if_missing(context);
  if (managed_group_) {
    if (is_dictified()) {
      managed_group_->add_to_group(column_pointers_->get_abstract());
      dict_->add_to_group(managed_group_);
    } else if (is_materialized()) {
      plain_data_->add_to_group(managed_group_);
    }
  }
}

void column::deregister_from_managed_group() {
  if (managed_group_ != nullptr) {
    managed_group_->clear_group();
  }
}

void column::erase_column() {
  const auto& memory_manger{get_swap_info().memory_manager()};
  if (memory_manger != nullptr) {
    memory_manger->erase_persistent(managed_group_);
  }
}

bool column::belongs_to_augmentation_table() const noexcept {
  return owner_ != nullptr && owner_->get_meta_data().is_augmentation_table();
}

column_info column::dump_header() const {
  column_info col_dump;
  col_dump.name = config_.name;
  col_dump.id = config_.id;
  col_dump.cache_key = config_.cache_key;
  col_dump.domain_column = config_.name;
  col_dump.format = processing_state_.get_format();
  col_dump.type = config_.type;
  return col_dump;
}

legacy_embedded_ctl::dynamic_bitset<> get_null_flags_copy(const column_t& column,
                                                          const common::execution_context& context) {
  auto null_flags{
      memory::tracking::make_tracked_dynamic_bitset_t(static_cast<size_t>(column->get_row_count(context)), context)};
  column->project_null_flags(null_flags, context);
  return null_flags;
}

void column::check_consistency_for_testing(const std::unordered_set<table*>& tables) const {
  for (table* table : dependencies_) {
    common::runtime_assert(tables.contains(table), "Runtime Assertion failed");
  }
  if (owner_ != nullptr) {
    common::runtime_assert(tables.contains(owner_), "Runtime Assertion failed");
  }
}

column::~column() {
  if (remove_swap_files_on_destruct_) {
    switch (status_.load()) {
      case column_loading::column_status::DICTIFIED:
        dict_->set_delete_from_disk_when_destructed(true);
        column_pointers_->set_delete_from_disk_when_destructed(true);
        break;
      case column_loading::column_status::MATERIALIZED:
        plain_data_->set_delete_from_disk_when_destructed(true);
        break;
      case column_loading::column_status::MISSING:
        break;
    }
  }
}

}  // namespace celonis::accelerator::memory
