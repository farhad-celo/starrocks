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
#include "types/uuid/uuid_storage.h"

/**
 * Storage for an array of data. The data can be compressed or swapped out.
 */
namespace celonis::accelerator::memory::management {

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(ctl::static_array<T> data,
                                                               const std::string& description) {
  return create_data_handler(ctl::shared_static_array<T>{std::move(data)}, description);
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(ctl::static_array<T> data) {
  const auto data_size{data.size()};
  return raw_data_handler_t<T>(new raw_data_handler(load_status::LOADED, std::move(data), data_size, ""));
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(const ctl::shared_static_array<T>& data,
                                                               const std::string& description) {
  const auto data_size{data.size()};
  raw_data_handler_t<T> raw_data(new raw_data_handler(load_status::LOADED, data, data_size, description));
  return raw_data;
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(const ctl::shared_static_array<T>& data) {
  raw_data_handler_t<T> raw_data(new raw_data_handler(load_status::LOADED, data, data.size(), ""));
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
                                      std::string description)
    : status(status), data(std::move(data)), size(size), desc(std::move(description)) {
  debug_assert(status == load_status::LOADED);
  loaded_by = std::this_thread::get_id();
  loaded_at = mem_clock_t::now();
}

template <typename T>
const_data_accessor<T> raw_data_handler<T>::get_const_data(const common::execution_context& context) requires(
    requires(ctl::shared_static_array<T> ptr) { const_data_accessor<T>{ptr}; }) {
  last_usage = mem_clock_t::now();
  usage_count++;

  return const_data_accessor<T>{data};
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
