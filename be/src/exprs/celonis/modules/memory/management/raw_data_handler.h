#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include <ctl/static_array_fwd.h>

#include "modules/common/execution_context.h"
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/raw_data_handler_fwd.h"
#include "modules/memory/types.h"

/**
 * Storage for an array of data. The data can be compressed or swapped out.
 */
namespace celonis::accelerator::memory::management {

template <typename T>
class raw_data_handler final : public data_handler {
 public:
  static constexpr const char* RAW_DATA_HANDLER_SWAP_IN_KEY = "RAW_DATA_HANDLER_SWAP_IN";
  static constexpr const char* RAW_DATA_HANDLER_SWAP_OUT_KEY = "RAW_DATA_HANDLER_SWAP_OUT";

  static raw_data_handler_t<T> create_data_handler(ctl::static_array<T> data, const std::string& description);

  static raw_data_handler_t<T> create_temp_data_handler(ctl::static_array<T> data);

  static raw_data_handler_t<T> create_data_handler(const ctl::shared_static_array<T>& data,
                                                   const std::string& description);

  [[nodiscard]] static std::shared_ptr<raw_data_handler<T>> create_temp_data_handler(
      const ctl::shared_static_array<T>& data);

  load_status get_load_status() const override { return status; };

  size_t get_size_in_memory() const override;

  size_t get_size() const { return size; };

  size_t get_usage_count() const override { return usage_count; };

  [[nodiscard]] usage_time_t get_last_usage() const override { return usage_time_t{last_usage}; }

  std::thread::id get_loaded_by() const override { return loaded_by; }

  [[nodiscard]] load_time_t get_loaded_at() const override { return load_time_t{loaded_at}; }

  using const_data_accessor_t = const_data_accessor<T>;

  /**
   * This function returns a pointer to the immutable stored data.
   */
  const_data_accessor_t get_const_data(const common::execution_context& context = {}) requires(
      requires(ctl::shared_static_array<T> ptr) { const_data_accessor<T>{ptr}; });

  ~raw_data_handler() override;

 private:
  raw_data_handler(load_status status, ctl::shared_static_array<T> data, size_t size, std::string description);

  mutable std::shared_mutex data_mutex;
  std::atomic<load_status> status;
  ctl::shared_static_array<T> data;
  std::atomic<size_t> size;
  std::atomic<mem_time_t> last_usage{mem_clock_t::now()};
  std::atomic<std::thread::id> loaded_by{std::thread::id{}};
  std::atomic<mem_time_t> loaded_at{mem_clock_t::time_point{}};
  std::atomic<size_t> usage_count{0};
  std::string desc;
};
}  // namespace celonis::accelerator::memory::management
