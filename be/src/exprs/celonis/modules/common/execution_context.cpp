#include "execution_context.h"

#ifndef CELOSTAR
#include <chrono>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/query/communication.pb.h"
#endif

namespace celonis::accelerator::common {

#ifdef CELOSTAR
// TODO(j.kim): Add a proper context and a trace.
execution_context::execution_context() noexcept {}

execution_context execution_context::create_sub_context(const std::string& operation_name,
                                                        const std::string& tags) const noexcept {
  return execution_context();
}
#else
execution_context::execution_context() noexcept : span_() {}

execution_context::execution_context(const std::string& operation_name,
                                     ctl::abstract_strategy_t memory_tracking_strategy) noexcept
    : span_(operation_name, {}), memory_tracking_strategy_{std::move(memory_tracking_strategy)} {}

execution_context::execution_context(const std::string& operation_name,
                                     const CommunicationRequest_ExecutionContext& remote_context,
                                     ctl::abstract_strategy_t memory_tracking_strategy) noexcept
    : span_(remote_context.has_span_context() ? tracing::span(operation_name, {}, remote_context.span_context())
                                              : tracing::span(operation_name, {})),
      memory_tracking_strategy_{std::move(memory_tracking_strategy)} {}

execution_context::execution_context(tracing::span&& managed_span, const execution_context* parent) noexcept
    : span_(std::move(managed_span)), parent_(parent), memory_tracking_strategy_{parent_->memory_tracking_strategy_} {}

execution_context execution_context::create_sub_context(const std::string& operation_name,
                                                        const tracing::tags_t& tags) const noexcept {
  return execution_context{span_.start_child_span(operation_name, tags), this};
}

execution_context::execution_context(execution_context&& other_context) noexcept
    : span_(std::move(other_context.span_)),
      memory_tracking_strategy_{std::move(other_context.memory_tracking_strategy_)} {
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

void execution_context::set_memory_tracking_strategy(ctl::abstract_strategy_t memory_tracking_strategy) {
  memory_tracking_strategy_ = std::move(memory_tracking_strategy);
}

const ctl::abstract_strategy_t& execution_context::get_memory_tracking_strategy() const {
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

execution_context::~execution_context() {
  if (parent_ != nullptr && !warnings_.empty()) {
    parent_->merge_warnings(std::move(warnings_));
  }
}
#endif

}  // namespace celonis::accelerator::common
