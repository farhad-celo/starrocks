#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "modules/common/execution_context_fwd.h"
#include "modules/common/timer.h"
#include "modules/memory/management/data_handler_fwd.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory::management {

void drop_short_wait_spans(const common::timer& timer_with_lock, common::execution_context& wait_context);

constexpr std::chrono::system_clock::duration SWAP_IN_MAX_DURATION{std::chrono::seconds(60)};

void log_warning_if_swap_in_too_long(
    common::timer& timer_with_lock, common::timer& timer_after_lock, const std::string& description,
    std::chrono::system_clock::duration max_timer_with_lock_duration = SWAP_IN_MAX_DURATION);

const std::string FILE_FORMAT_VERSION_SUFFIX = "4";                            // NOLINT(cert-err58-cpp)
const std::string DICT_ENDING = ".dic" + FILE_FORMAT_VERSION_SUFFIX;           // NOLINT(cert-err58-cpp)
const std::string COLUMN_PTR_ENDING = ".cp" + FILE_FORMAT_VERSION_SUFFIX;      // NOLINT(cert-err58-cpp)
const std::string NULL_FLAGS_ENDING = ".np" + FILE_FORMAT_VERSION_SUFFIX;      // NOLINT(cert-err58-cpp)
const std::string NULL_FLAGS_NEW_ENDING = ".nf" + FILE_FORMAT_VERSION_SUFFIX;  // NOLINT(cert-err58-cpp)
// A replacement format for the NULL_FLAGS_ENDING and NULL_FLAGS_NEW_ENDING, in which bitset
// is stored in uint64 dense array instead of boolean array
const std::string BITSET_ENDING = ".bs" + FILE_FORMAT_VERSION_SUFFIX;                 // NOLINT(cert-err58-cpp)
const std::string MATERIALIZED_DATA_ENDING = ".data" + FILE_FORMAT_VERSION_SUFFIX;    // NOLINT(cert-err58-cpp)
const std::string MATERIALIZED_DATA_NEW_ENDING = ".ri" + FILE_FORMAT_VERSION_SUFFIX;  // NOLINT(cert-err58-cpp)
const std::string STRING_BUFFER_ENDING = ".bfr" + FILE_FORMAT_VERSION_SUFFIX;         // NOLINT(cert-err58-cpp)
const std::string DICT_STR_BUFFER_ENDING = ".ubfr" + FILE_FORMAT_VERSION_SUFFIX;      // NOLINT(cert-err58-cpp)

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

  [[nodiscard]] virtual persistence_status get_persistence_status() const = 0;

  /**
   * Report broken swap file
   */
  [[nodiscard]] virtual bool swap_file_broken() const = 0;

#ifndef CELOSTAR
  virtual bool swap_out(common::execution_context& context) = 0;
#endif

  virtual void swap_in(const common::execution_context& context) = 0;

#ifndef CELOSTAR
  virtual bool compress() = 0;
#endif

  [[nodiscard]] virtual size_t get_size_in_memory() const = 0;

  [[nodiscard]] virtual size_t get_size_on_disk() const = 0;

  [[nodiscard]] virtual size_t get_usage_count() const = 0;

  [[nodiscard]] virtual bool is_persisted() const = 0;

  [[nodiscard]] virtual bool is_swappable() const = 0;

  [[nodiscard]] virtual usage_time_t get_last_usage() const = 0;

  [[nodiscard]] virtual std::thread::id get_loaded_by() const = 0;

  [[nodiscard]] virtual load_time_t get_loaded_at() const = 0;

  [[nodiscard]] virtual std::string description() const = 0;

  virtual ~data_handler() = default;

  static bool swap_file_exists(const std::string& swap_file, const swap_info& sinfo);
};
}  // namespace celonis::accelerator::memory::management
