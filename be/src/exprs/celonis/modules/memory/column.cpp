#include "column.h"

#include <algorithm>
#include <memory>

#include <fmt/format.h>
#include <tbb/enumerable_thread_specific.h>

#include <ctl/utility.h>

#include "concurrency/concurrency_utils.h"
#include "log/log.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/timer.h"
#include "modules/memory/table.h"
#include "modules/memory/transform/dictifier.h"

namespace celonis::accelerator::memory {

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
  return config_.row_count;
}

void column::dictify_if_needed(const common::execution_context& context) {
  // #lizard forgives
  if (is_dictified()) {
    return;
  }

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
}

row_id column::get_domain_count(const common::execution_context& context,
                                const no_dictify_request_t& no_dictify_request) {
  dictify_if_needed(context);
  if (dict_ == nullptr) {
    return 0;
  }
  return dict_->get_size();
}

std::shared_ptr<materialized_data> column::get_materialized_data(const common::execution_context& context) {
  std::shared_lock lck(column_mutex_);

  if (!(is_materialized())) {
    return std::shared_ptr<materialized_data>();
  }
  return plain_data_;
}

}  // namespace celonis::accelerator::memory
