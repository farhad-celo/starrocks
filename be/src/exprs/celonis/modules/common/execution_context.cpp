#include "execution_context.h"

#include <chrono>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/cube/extended_tables.h"
#include "modules/memory/table.h"
#ifndef CELOSTAR
#include "modules/query/communication.pb.h"
#endif

namespace celonis::accelerator::common {

execution_context::execution_context()
    : span_{std::make_shared<tracing::span>()},
      table_to_user_visible_name_mapping_{std::make_shared<cube::table_to_user_visible_name_mapping>()},
      extended_tables_{std::make_shared<cube::extended_tables>()} {}

execution_context::execution_context(const std::string& operation_name,
                                     legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy)
    : span_{std::make_shared<tracing::span>(operation_name, tracing::tags_t{})},
      memory_tracking_strategy_{std::move(memory_tracking_strategy)},
      table_to_user_visible_name_mapping_{std::make_shared<cube::table_to_user_visible_name_mapping>()},
      extended_tables_{std::make_shared<cube::extended_tables>()} {}

#ifndef CELOSTAR
execution_context::execution_context(const std::string& operation_name,
                                     const CommunicationRequest_ExecutionContext& remote_context,
                                     legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy) noexcept
    : span_{remote_context.has_span_context()
                ? std::make_shared<tracing::span>(operation_name, tracing::tags_t{}, remote_context.span_context())
                : std::make_shared<tracing::span>(operation_name, tracing::tags_t{})},
      memory_tracking_strategy_{std::move(memory_tracking_strategy)},
      table_to_user_visible_name_mapping_{std::make_shared<cube::table_to_user_visible_name_mapping>()},
      extended_tables_{std::make_shared<cube::extended_tables>()} {}
#endif

execution_context::execution_context(tracing::span&& managed_span, const execution_context* parent)
    : parent_{parent},
      span_{std::make_shared<tracing::span>(std::move(managed_span))},
      memory_tracking_strategy_{parent_->memory_tracking_strategy_},
      table_to_user_visible_name_mapping_{
          parent->table_to_user_visible_name_mapping_.lock_shared([](const auto& mapping) { return mapping; })},
      extended_tables_{parent_->extended_tables_.lock_shared([](const auto& tables) { return tables; })} {}

execution_context::execution_context(const execution_context* parent)
    : parent_{parent},
      span_{parent_->span_},
      memory_tracking_strategy_{parent_->memory_tracking_strategy_},
      table_to_user_visible_name_mapping_{
          parent_->table_to_user_visible_name_mapping_.lock_shared([](const auto& mapping) { return mapping; })},
      extended_tables_{parent_->extended_tables_.lock_shared([](const auto& tables) { return tables; })} {}

execution_context execution_context::create_sub_context(const std::string& operation_name,
                                                        const tracing::tags_t& tags) const {
  return execution_context{span_->start_child_span(operation_name, tags), this};
}

execution_context execution_context::create_sub_context_with_same_span() const { return execution_context{this}; }

execution_context::execution_context(execution_context&& other_context) noexcept
    : span_{std::move(other_context.span_)},
      memory_tracking_strategy_{std::move(other_context.memory_tracking_strategy_)},
      table_to_user_visible_name_mapping_{other_context.table_to_user_visible_name_mapping_.lock_mutable(
          [](const auto& mapping) { return std::move(mapping); })},
      extended_tables_{
          other_context.extended_tables_.lock_mutable([](const auto& tables) { return std::move(tables); })} {
  /*
   * In the most common use case the move-constructor is used for creating a new sub context which doesn't have any
   * warnings yet. There is no need to move any warnings, therefore avoid acquiring an unique_lock.
   */
  if (!other_context.get_warnings().empty()) {
    std::unique_lock<std::shared_timed_mutex> other_warnings_lock(other_context.warnings_mutex_,
                                                                  std::chrono::seconds(60));
    if (!other_warnings_lock.owns_lock()) {
      // Only log but don't throw to ensure an exception-safe move constructor
      log::error("Warnings are not moved because acquiring a lock failed at {}", __func__);
    } else {
      warnings_ = std::move(other_context.warnings_);
    }
  }
}

void execution_context::set_memory_tracking_strategy(
    legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy) {
  memory_tracking_strategy_ = std::move(memory_tracking_strategy);
}

const legacy_embedded_ctl::abstract_strategy_t& execution_context::get_memory_tracking_strategy() const {
  return memory_tracking_strategy_;
}

void execution_context::add_warning(const std::string& warning) {
  std::unique_lock<std::shared_timed_mutex> change_warnings_lock(warnings_mutex_, std::chrono::seconds(60));
  if (!change_warnings_lock.owns_lock()) {
    log::error("Acquiring lock for adding warning failed at {}", __func__);
    throw common::cpm_exception{"Adding warning failed."};
  }
  warnings_.emplace(warning);
}

void execution_context::add_warnings(const memory::warnings_container_t& warnings) {
  std::unique_lock<std::shared_timed_mutex> change_warnings_lock(warnings_mutex_, std::chrono::seconds(60));
  if (!change_warnings_lock.owns_lock()) {
    log::error("Acquiring lock for adding warning failed at {}", __func__);
    throw common::cpm_exception{"Adding warnings failed."};
  }
  for (const auto& warning : warnings) {
    warnings_.emplace(warning);
  }
}

void execution_context::add_warnings(const memory::warnings_t& warnings) {
  if (warnings) {
    add_warnings(*warnings);
  }
}
void execution_context::merge_warnings(memory::warnings_container_t warnings) const noexcept {
  std::unique_lock<std::shared_timed_mutex> change_warnings_lock(warnings_mutex_, std::chrono::seconds(60));
  if (!change_warnings_lock.owns_lock()) {
    log::error("Acquiring lock for merging warnings failed at {}", __func__);
  }
  warnings_.merge(std::move(warnings));
}

memory::warnings_container_t execution_context::get_warnings() const noexcept {
  std::shared_lock<std::shared_timed_mutex> get_warnings_lock(warnings_mutex_);
  return warnings_;
}

void execution_context::add_user_visible_name_mapping(const memory::table* table,
                                                      memory::user_visible_table_name name) {
  table_to_user_visible_name_mapping_.lock_mutable(
      [&table, &name](const auto& mapping) { mapping->add(table, std::move(name)); });
}

std::optional<memory::user_visible_table_name> execution_context::lookup_user_visible_name(
    const memory::table* table) const {
  return table_to_user_visible_name_mapping_.lock_shared(
      [&table](const auto& mapping) { return mapping->lookup(table); });
}

void execution_context::add_column_to_extended_table(const memory::table* table, const std::string& column_name,
                                                     memory::column_t column) {
  extended_tables_.lock_mutable([&table, &column_name, &column](auto& extended_tables) {
    extended_tables->add(table, column_name, std::move(column));
  });
}

std::optional<memory::column_t> execution_context::lookup_column_from_extended_table(
    const memory::table* table, const std::string_view column_name) const {
  return extended_tables_.lock_shared(
      [&table, &column_name](const auto& extended_tables) { return extended_tables->lookup(table, column_name); });
}

std::vector<std::string> execution_context::get_columns_from_extended_table(const memory::table* table) const {
  return extended_tables_.lock_shared(
      [&table](const auto& extended_tables) { return extended_tables->get_columns(table); });
}

execution_context::~execution_context() {
  if (parent_ != nullptr && !warnings_.empty()) {
    parent_->merge_warnings(std::move(warnings_));
  }
}

}  // namespace celonis::accelerator::common
