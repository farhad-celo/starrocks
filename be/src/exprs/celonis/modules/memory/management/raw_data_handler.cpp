#include "raw_data_handler.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ctl/assert.h>
#include <ctl/static_array.h>

#include "concurrency/concurrency_utils.h"
#include "log/log.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/execution_context.h"
#include "modules/common/timer.h"
#include "modules/common/tracing/span.h"
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/swap_info.h"
#include "types/uuid/uuid_storage.h"

/**
 * Storage for an array of data. The data can be compressed or swapped out.
 */
namespace celonis::accelerator::memory::management {

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(ctl::static_array<T> data, const std::string& swap_file,
                                                               const swap_info& sinfo, const std::string& description) {
  return create_data_handler(ctl::shared_static_array<T>{std::move(data)}, swap_file, sinfo, description);
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(ctl::static_array<T> data) {
  const auto data_size{data.size()};
  return raw_data_handler_t<T>(
      new raw_data_handler(load_status::LOADED, std::move(data), data_size, "", no_swap(), false, 0, ""));
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(const ctl::shared_static_array<T>& data,
                                                               const std::string& swap_file, const swap_info& sinfo,
                                                               const std::string& description) {
  const auto data_size{data.size()};
  raw_data_handler_t<T> raw_data(
      new raw_data_handler(load_status::LOADED, data, data_size, swap_file, sinfo, false, 0, description));
  return raw_data;
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(const ctl::shared_static_array<T>& data) {
  raw_data_handler_t<T> raw_data(
      new raw_data_handler(load_status::LOADED, data, data.size(), "", no_swap(), false, 0, ""));
  return raw_data;
}

template <typename T>
size_t raw_data_handler<T>::get_size_in_memory() const {
  debug_assert(status == load_status::LOADED);
  return size * sizeof(T);
}

template <typename T>
raw_data_handler<T>::~raw_data_handler() = default;

template <typename T>
raw_data_handler<T>::raw_data_handler(load_status status, ctl::shared_static_array<T> data, size_t size,
                                      std::string swap_file, swap_info swap_information, bool persisted,
                                      size_t size_on_disk, std::string description)
    : status(status),
      data(std::move(data)),
      size(size),
      size_on_disk(size_on_disk),
      swap_file(std::move(swap_file)),
      swap_information(std::move(swap_information)),
      persisted(persisted),
      desc(std::move(description)) {
  debug_assert(status == load_status::LOADED);
  loaded_by = std::this_thread::get_id();
  loaded_at = mem_clock_t::now();
}

template <typename T>
ctl::shared_static_array<T> raw_data_handler<T>::swap_in_data(const common::execution_context& context) {
  auto wait_context = context.create_sub_context("swap_in_wait_for_lock", {});
  common::timer timer_with_lock;

  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status == load_status::LOADED) {
      // nothing to do
      drop_short_wait_spans(timer_with_lock, wait_context);
      return data;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  common::timer timer_after_lock;
  drop_short_wait_spans(timer_with_lock, wait_context);
  wait_context.get_span().finish_span();
  // check status after lock is acquired
  debug_assert(status == load_status::LOADED);
  return data;
}

template <typename T>
const_data_accessor<T> raw_data_handler<T>::get_const_data(const common::execution_context& context) requires(
    requires(ctl::shared_static_array<T> ptr) { const_data_accessor<T>{ptr}; }) {
  last_usage = mem_clock_t::now();
  usage_count++;

  // TODO(j.boettcher) remove this redundant if when we migrate to clang 15. This additional requires check is only
  // necessary for clang 14
  constexpr bool supports_const_data_accessor = requires() { const_data_accessor<T>{swap_in_data(context)}; };
  if constexpr (supports_const_data_accessor) {
    return const_data_accessor<T>{swap_in_data(context)};
  } else {
    ctl::assert_unreachable(ctl::source_location());
  }
}

template class raw_data_handler<date::celonis_date_storage>;
template class raw_data_handler<types::uuid::uuid_storage>;
template class raw_data_handler<cel_null_t>;
template class raw_data_handler<bool>;
template class raw_data_handler<char>;
template class raw_data_handler<int8_t>;
template class raw_data_handler<int16_t>;
template class raw_data_handler<int32_t>;
template class raw_data_handler<int64_t>;
template class raw_data_handler<double>;
template class raw_data_handler<uint16_t>;
template class raw_data_handler<uint32_t>;
template class raw_data_handler<uint64_t>;

}  // namespace celonis::accelerator::memory::management
