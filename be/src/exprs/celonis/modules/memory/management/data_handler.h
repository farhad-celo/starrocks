#pragma once

#include <chrono>
#include <string>
#include <thread>

#include "modules/common/execution_context_fwd.h"
#include "modules/common/timer.h"
#include "modules/memory/management/data_handler_fwd.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory::management {

void drop_short_wait_spans(const common::timer& timer_with_lock, common::execution_context& wait_context);

constexpr std::chrono::system_clock::duration SWAP_IN_MAX_DURATION{std::chrono::seconds(60)};

void log_warning_if_swap_in_too_long(
    common::timer& timer_with_lock, common::timer& timer_after_lock, const std::string& description,
    std::chrono::system_clock::duration max_timer_with_lock_duration = SWAP_IN_MAX_DURATION);

constexpr auto DICT_DESC = " Dictionary";
constexpr auto COLUMN_PTR_DESC = " Column Pointers";
constexpr auto NULL_FLAGS_DESC = " Null Flags";
constexpr auto MATERIALIZED_DATA_DESC = " Data";
constexpr auto STRING_BUFFER_DESC = " String Buffer";
constexpr auto DICT_STR_BUFFER_DESC = " Unique Strings Buffer";

constexpr auto LOCK_LOGGING_THRESHOLD = std::chrono::seconds{600};

// Interface for data which should be managed by the memory manager.
class data_handler {
 public:
  [[nodiscard]] virtual load_status get_load_status() const = 0;

  [[nodiscard]] virtual size_t get_size_in_memory() const = 0;

  [[nodiscard]] virtual size_t get_usage_count() const = 0;

  [[nodiscard]] virtual usage_time_t get_last_usage() const = 0;

  [[nodiscard]] virtual std::thread::id get_loaded_by() const = 0;

  [[nodiscard]] virtual load_time_t get_loaded_at() const = 0;

  virtual ~data_handler() = default;
};
}  // namespace celonis::accelerator::memory::management
