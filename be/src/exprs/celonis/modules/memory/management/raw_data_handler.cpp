#include "raw_data_handler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <fmt/core.h>

#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "log/log.h"
#ifndef CELOSTAR
#include "modules/common/call_and_log_unsafe_callable.h"
#endif
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/common/timer.h"
#include "modules/common/tracing/span.h"
#ifndef CELOSTAR
#include "modules/io/byte_iterator.h"
#include "modules/io/byte_view_utils.h"
#include "modules/io/compress_encrypt_utils.h"
#include "modules/io/file_utils.h"
#include "modules/io/storage_manager.h"
#include "modules/io/swap_data_types.h"
#endif
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#ifndef CELOSTAR
#include "modules/memory/management/swap_context.h"
#endif
#include "modules/memory/management/swap_file_utils.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/row_id.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "types/uuid/uuid_storage.h"

/**
 * Storage for an array of data. The data can be compressed or swapped out.
 */
namespace celonis::accelerator::memory::management {

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(legacy_embedded_ctl::static_array<T> data,
                                                               const std::string& swap_file, const swap_info& sinfo,
                                                               const std::string& description) {
  return create_data_handler(legacy_embedded_ctl::shared_static_array<T>{std::move(data)}, swap_file, sinfo,
                             description);
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(legacy_embedded_ctl::static_array<T> data) {
  const auto data_size{data.size()};
  return raw_data_handler_t<T>(
      new raw_data_handler(load_status::LOADED, std::move(data), data_size, "", no_swap(), false, 0, ""));
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_data_handler(const legacy_embedded_ctl::shared_static_array<T>& data,
                                                               const std::string& swap_file, const swap_info& sinfo,
                                                               const std::string& description) {
  const auto data_size{data.size()};
  raw_data_handler_t<T> raw_data(
      new raw_data_handler(load_status::LOADED, data, data_size, swap_file, sinfo, false, 0, description));
  return raw_data;
}

template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::create_temp_data_handler(
    const legacy_embedded_ctl::shared_static_array<T>& data) {
  raw_data_handler_t<T> raw_data(
      new raw_data_handler(load_status::LOADED, data, data.size(), "", no_swap(), false, 0, ""));
  return raw_data;
}

#ifndef CELOSTAR
template <typename T>
raw_data_handler_t<T> raw_data_handler<T>::init_from_swap(const std::string& swap_file, const swap_info& sinfo,
                                                          const std::string& description) {
  if (data_handler::swap_file_exists(swap_file, sinfo)) {
    const size_t size_on_disk{io::file_size(swap_file, sinfo)};
    const io::storage_manager& sm{sinfo.storage_manager()};
    const io::storage_manager::read_return_data<T> read_result{
        sm.read_compressed_mt<T>(swap_file, sinfo, size_on_disk, io::storage_manager::read_mode::SKIP_DATA)};
    raw_data_handler_t<T> raw_data(new raw_data_handler(load_status::SWAPPED, {}, read_result.size, swap_file, sinfo,
                                                        true, size_on_disk, description));
    return raw_data;
  }

  return {};
}

template <typename T>
bool raw_data_handler<T>::compress() {
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status != load_status::LOADED) {
      return false;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  // check status after lock is acquired
  if (status != load_status::LOADED) {
    return false;
  }

  if (data.use_count() > 1) {
    return false;
  }

  io::byte_iterator iter{io::create_byte_iterator_from(std::span<const T>(data))};
  init_compressed_state(io::compress_to_memory_mt(iter));
  return true;
}
#endif

template <typename T>
size_t raw_data_handler<T>::get_size_in_memory() const {
#ifdef CELOSTAR
  legacy_embedded_debug_assert(status == load_status::LOADED);
  return size * sizeof(T);
#else
  if (status == load_status::LOADED) {
    return size * sizeof(T);
  }

  switch (const auto lck{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)}; status.load()) {
    case load_status::LOADED:
      return size * sizeof(T);
    case load_status::COMPRESSED:
      return compressed_data().size();
    case load_status::SWAPPED:
      return 0;
    default:
      throw common::internal_exception{"Unknown load status [{} ({})].", to_string(status.load()),
                                       legacy_embedded_ctl::enum_to_underlying_type(status.load())};
  }
#endif
}

#ifndef CELOSTAR
template <typename T>
bool raw_data_handler<T>::swap_out(common::execution_context& context) {
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status == load_status::SWAPPED) {
      return true;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  if (!swap_information.is_swappable()) {
    return false;
  }

  if (status == load_status::SWAPPED) {
    return true;
  }
  // swap out the column pointers
  // column pointers can be shared between a column and an aliased column, so we need to make sure that it's not
  // swapped out when still in use. it wouldn't cause any real problems, but when the column is used again, it will
  // create a new memory array which will duplicate the memory used by the column to hold a new array of pointers.
  if (data.use_count() > 1) {
    return false;
  }

  swap_to_disk(context);
  return persisted;
}

template <typename T>
void raw_data_handler<T>::write_out(const common::execution_context& context) {
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status == load_status::SWAPPED) {
      return;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  if (!swap_information.is_swappable()) {
    return;
  }

  if (status == load_status::SWAPPED) {
    return;
  }

  write_to_disk(context);
}
#endif

template <typename T>
raw_data_handler<T>::~raw_data_handler() {
#ifndef CELOSTAR
  if (swap_information.is_swappable()) {
    if (persisted && delete_from_disk_when_destructed_.load()) {
      const io::storage_manager& sm{swap_information.storage_manager()};
      try {
        sm.delete_swap_file(swap_file, swap_information);
      } catch (const std::exception& e) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler. Reason: [{}].", swap_file,
                  e.what());
      } catch (...) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler.", swap_file);
      }
    }

    common::call_and_log_unsafe_callable(
        [this]() { swap_information.storage_manager().deregister_file(swap_file, swap_information, description()); },
        fmt::format("Couldn't deregister file: {}", swap_file));
  }
#endif
}

template <typename T>
raw_data_handler<T>::raw_data_handler(load_status status, legacy_embedded_ctl::shared_static_array<T> data, size_t size,
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
#ifdef CELOSTAR
  legacy_embedded_debug_assert(status == load_status::LOADED);
  loaded_by = std::this_thread::get_id();
  loaded_at = mem_clock_t::now();
#else
  const auto& sinfo = this->swap_information;
  if (sinfo.is_swappable()) {
    sinfo.storage_manager().register_file(this->swap_file, sinfo, desc);
  }

  if (status == load_status::LOADED) {
    loaded_by = std::this_thread::get_id();
    loaded_at = mem_clock_t::now();
  }

  if (size > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
    log::jinfo("Loading large swap file.", to_json_swap_file_info(this->swap_file, desc, size, size_on_disk));
  }
#endif
}

#ifndef CELOSTAR
namespace {
template <typename T>
io::storage_manager::read_return_data<T> read_from_swap(swap_info& swap_info, const std::string& swap_file,
                                                        const std::string& desc, const size_t size_on_disk) {
  const io::storage_manager& sm = swap_info.storage_manager();
  if (!data_handler::swap_file_exists(swap_file, swap_info)) {
    log::jerror("Could not find swap file.", {{"swap_file", swap_file}, {"description", desc}});
    throw common::file_exception{"Could not find swap file: {}.", swap_file};
  }
  return sm.read_compressed_mt<T>(swap_file, swap_info, size_on_disk);
}
}  // namespace
#endif

template <typename T>
legacy_embedded_ctl::shared_static_array<T> raw_data_handler<T>::swap_in_data(
    const common::execution_context& context) {
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
#ifdef CELOSTAR
  legacy_embedded_debug_assert(status == load_status::LOADED);
  return data;
#else
  if (status == load_status::LOADED) {
    return data;
  }

  // measure time for swap_in
  auto swap_context = create_swap_in_context(context, status, swap_file, description(), size * sizeof(T), size_on_disk,
                                             compressed_data_size());

  if (status == load_status::COMPRESSED) {
    // allocate memory according to old capacity.
    auto decompressed_data{memory::tracking::make_static_array_for_overwrite<T>(
        size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMORY_FOR_DECOMPRESSION_MSG, desc), context)};
    io::decompress_from_memory_mt(compressed_data(), io::as_byte_span(std::span{decompressed_data}));
    data = std::move(decompressed_data);
    compressed_data_.reset();
  } else if (status == load_status::SWAPPED) {
    try {
      io::storage_manager::read_return_data<T> read_result{
          read_from_swap<T>(swap_information, swap_file, desc, size_on_disk)};
      if (read_result.size != size) {
        log::error(
            "broken swap file: size recorded in swap_file {} differs from size stored in memory: {} vs. {} for {}",
            swap_file, read_result.size, size.load(), desc);
        throw common::file_exception{
            "broken swap file: size recorded in swap_file: {} differs from size stored in memory: [{}] vs. [{}].",
            swap_file, read_result.size, size.load()};
      }
      if (read_result.size > LARGE_SWAP_FILE_THRESHOLD) {
        log::jinfo("Swapping in large file.",
                   to_json_swap_file_info(swap_information.get_swap_file_path(swap_file), description(),
                                          read_result.size, size_on_disk.load()));
      }
      set_swap_in_read_size_tag(swap_context, read_result.size * sizeof(T));
      data = std::move(read_result.data);
      size = read_result.size;
      broken_swap_file = false;
    } catch (legacy_embedded_ctl::short_of_memory& e) {
      throw;
    } catch (legacy_embedded_ctl::bad_alloc& e) {
      throw;
    } catch (std::exception& e) {
      broken_swap_file = true;
      throw;
    }
  } else {
    throw common::internal_exception{"Unknown load status: [{}] at {}.",
                                     legacy_embedded_ctl::enum_to_underlying_type(status.load()),
                                     legacy_embedded_ctl::source_location{}};
  }

  status = load_status::LOADED;
  loaded_by = std::this_thread::get_id();
  loaded_at = mem_clock_t::now();

  log_warning_if_swap_in_too_long(timer_with_lock, timer_after_lock, description());
  add_swap_invocation_to_operator_statistics(swap_information.memory_manager(), RAW_DATA_HANDLER_SWAP_IN_KEY,
                                             timer_after_lock.duration());
  return data;
#endif
}

#ifndef CELOSTAR
template <typename T>
void raw_data_handler<T>::swap_to_disk(common::execution_context& context) {
  auto swap_out_context{create_swap_out_context(context, status, swap_file, description(), size * sizeof(T),
                                                size_on_disk, compressed_data_size())};
  if (!persisted) {
    write_to_disk(swap_out_context);
    update_swap_file_disk_size_tag(swap_out_context, size_on_disk.load());
  }
  data.reset();  // frees memory of data
  compressed_data_.reset();
  status = load_status::SWAPPED;
  loaded_by = std::thread::id{};
  loaded_at = std::chrono::steady_clock::time_point{};
}

template <typename T>
void raw_data_handler<T>::write_to_disk(const common::execution_context& context) {
  common::timer timer;
  const io::storage_manager& sm = swap_information.storage_manager();

  if (status == load_status::LOADED) {
    io::byte_iterator iter{io::create_byte_iterator_from(std::span<const T>{data})};
    size_on_disk = sm.compress_and_write_mt(swap_file, iter, sizeof(T) * size, swap_information, io::get_type_id<T>(),
                                            context, false);
  } else if (status == load_status::COMPRESSED) {
    size_on_disk = sm.encrypt_and_write_mt(swap_file, compressed_data(), sizeof(T) * size, swap_information,
                                           io::get_type_id<T>(), context);
  } else {
    throw common::internal_exception{"Unknown load status: [{}] at {}.",
                                     legacy_embedded_ctl::enum_to_underlying_type(status.load()),
                                     legacy_embedded_ctl::source_location{}};
  }
  persisted = true;
  broken_swap_file = false;
  timer.stop();
  add_swap_invocation_to_operator_statistics(swap_information.memory_manager(), RAW_DATA_HANDLER_SWAP_OUT_KEY,
                                             timer.duration());
}

template <typename T>
[[nodiscard]] const io::compressed_data& raw_data_handler<T>::compressed_data() const {
  legacy_embedded_debug_assert(status == load_status::COMPRESSED);
  legacy_embedded_debug_assert(compressed_data_.has_value());
  legacy_embedded_debug_assert(data.empty());
  return *compressed_data_;
}

template <typename T>
[[nodiscard]] size_t raw_data_handler<T>::compressed_data_size() const {
  if (status == load_status::COMPRESSED) {
    return compressed_data().size();
  }
  legacy_embedded_debug_assert(!compressed_data_.has_value());
  return 0;
}
#endif

template <typename T>
const_data_accessor<T> raw_data_handler<T>::get_const_data(const common::execution_context& context) requires(
    requires(legacy_embedded_ctl::shared_static_array<T> ptr) { const_data_accessor<T>{ptr}; }) {
  last_usage = mem_clock_t::now();
  usage_count++;

  // TODO(j.boettcher) remove this redundant if when we migrate to clang 15. This additional requires check is only
  // necessary for clang 14
  constexpr bool supports_const_data_accessor = requires() { const_data_accessor<T>{swap_in_data(context)}; };
  if constexpr (supports_const_data_accessor) {
    return const_data_accessor<T>{swap_in_data(context)};
  } else {
    legacy_embedded_ctl::assert_unreachable(legacy_embedded_ctl::source_location());
  }
}

#ifndef CELOSTAR
template <typename T>
inline void raw_data_handler<T>::init_compressed_state(io::compressed_data&& compressed_data) {
  compressed_data_ = std::move(compressed_data);
  data.reset();  // frees memory of data
  status = load_status::COMPRESSED;
}
#endif

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
