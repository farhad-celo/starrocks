#include "swappable_bitset.h"

#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <fmt/core.h>

#include "concurrency/concurrency_utils.h"
#include "ctl/exception.h"
#include "ctl/memory/memory_fwd.h"
#include "ctl/source_location.h"
#include "ctl/static_array.h"
#include "ctl/type_traits.h"
#include "ctl/utils/allocation_messages.h"
#include "ctl/utils/allocation_reason.h"
#include "format/json/json.h"
#include "log/log.h"
#include "modules/common/call_and_log_unsafe_callable.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/timer.h"
#ifndef CELOSTAR
#include "modules/io/file_utils.h"
#include "modules/io/storage_manager.h"
#include "modules/io/swap_data_types.h"
#endif
#include "modules/memory/management/const_bitset_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#ifndef CELOSTAR
#include "modules/memory/management/swap_context.h"
#endif
#include "modules/memory/management/swap_file_utils.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/row_id.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"

namespace celonis::accelerator::memory::management {
std::shared_ptr<swappable_bitset> swappable_bitset::create_data_handler(const memory::null_flags_t& data,
                                                                        const std::string& swap_file,
                                                                        const swap_info& sinfo,
                                                                        const std::string& description) {
  std::shared_ptr<swappable_bitset> raw_data(
      new swappable_bitset(load_status::LOADED, data, data->size(), swap_file, sinfo, false, 0, description));
  return raw_data;
}

#ifndef CELOSTAR
std::shared_ptr<swappable_bitset> swappable_bitset::init_from_swap(const std::string& swap_file, const swap_info& sinfo,
                                                                   const std::string& description) {
  if (swap_file_exists(swap_file, sinfo)) {
    const size_t size_on_disk = io::file_size(swap_file, sinfo);
    const io::storage_manager& sm = sinfo.storage_manager();
    const io::storage_manager::read_return_data<bool> read_result =
        sm.read_compressed_mt<bool>(swap_file, sinfo, size_on_disk, io::storage_manager::read_mode::SKIP_DATA);
    std::shared_ptr<swappable_bitset> raw_data(new swappable_bitset(load_status::SWAPPED, nullptr, read_result.size,
                                                                    swap_file, sinfo, true, size_on_disk, description));
    return raw_data;
  }
  return {};
}

bool swappable_bitset::swap_out(common::execution_context& context) {
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

  if (data.use_count() > 1) {
    return false;
  }

  swap_out_bitset(context);
  return persisted;
};

void swappable_bitset::write_out(common::execution_context& context) {
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status == load_status::SWAPPED) {
      log::info("Swappable bitset with swap file \"{}\" already swapped!", swap_file);
      return;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  if (!swap_information.is_swappable()) {
    return;
  }

  if (status == load_status::SWAPPED) {
    log::info("Swappable bitset with swap file \"{}\" already swapped!", swap_file);
    return;
  }

  write_to_disk(context);
}
#endif

swappable_bitset::const_data_accessor_t swappable_bitset::get_const_data(const common::execution_context& context) {
  last_usage = mem_clock_t::now();
  usage_count++;
  return const_data_accessor_t{load_from_swap(context), size};
}

bool swappable_bitset::is_swappable() const { return swap_information.is_swappable(); }

#ifndef CELOSTAR
bool swappable_bitset::compress() {
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status != load_status::LOADED) {
      return false;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

  if (status != load_status::LOADED) {
    return false;
  }

  status = load_status::COMPRESSED;  // we just pretend to compress. Data should be small anyway
  return true;
};
#endif

size_t swappable_bitset::get_size_in_memory() const {
  if (status == load_status::SWAPPED) {
    return 0;
  }
  return size / decltype(data)::element_type::BLOCK_SIZE +
         static_cast<size_t>(size % decltype(data)::element_type::BLOCK_SIZE != 0);
};

void swappable_bitset::set_delete_from_disk_when_destructed(const bool value) {
  delete_from_disk_when_destructed_.store(value);
}

swappable_bitset::~swappable_bitset() {
#ifndef CELOSTAR
  if (swap_information.is_swappable()) {
    if (persisted && delete_from_disk_when_destructed_.load()) {
      const io::storage_manager& sm{swap_information.storage_manager()};
      try {
        sm.delete_swap_file(swap_file, swap_information);
      } catch (const std::exception& e) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of swappable_bitset. Reason: [{}].", swap_file,
                  e.what());
      } catch (...) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of swappable_bitset.", swap_file);
      }
    }

    common::call_and_log_unsafe_callable(
        [this]() { swap_information.storage_manager().deregister_file(swap_file, swap_information); },
        fmt::format("Couldn't deregister file: {}", swap_file));
  }
#endif
};

#ifdef WIP
// TODO: Confirm that it's not loaded.
#endif
swappable_bitset::swappable_bitset(load_status status, memory::null_flags_t data, size_t size, std::string swap_file,
                                   swap_info swap_information, bool persisted, size_t size_on_disk,
                                   std::string description)
    : status(status),
      data(std::move(data)),
      size(size),
      size_on_disk(size_on_disk),
      last_usage(mem_clock_t::now()),
      loaded_by(std::thread::id{}),
      loaded_at(mem_time_t{}),
      swap_file(std::move(swap_file)),
      swap_information(std::move(swap_information)),
      persisted(persisted),
      usage_count(0),
      desc(std::move(description)) {
#ifndef CELOSTAR
  const auto& sinfo = this->swap_information;
  if (sinfo.is_swappable()) {
    sinfo.storage_manager().register_file(this->swap_file, sinfo, desc);
  }
#endif

  if (status == load_status::LOADED) {
    loaded_by = std::this_thread::get_id();
    loaded_at = mem_clock_t::now();
  }

  if (size > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
    log::jerror("Loading large swap file.", to_json_swap_file_info(this->swap_file, desc, size, size_on_disk));
  }
}

memory::null_flags_t swappable_bitset::load_from_swap(const common::execution_context& context) {
  common::timer timer_with_lock;
  {
    const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};
    if (status == load_status::LOADED) {
      return data;
    }
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex, LOCK_LOGGING_THRESHOLD)};

#ifdef CELOSTAR
  debug_assert(status == load_status::LOADED);
  return data;
#else
  common::timer timer_after_lock;
  if (status == load_status::LOADED) {
    return data;
  }

  // measure time for swap_in
  auto swap_context = create_swap_in_context(context, status.load(), swap_file, description(),
                                             size.load() * sizeof(bool), size_on_disk.load(), 0);

  if (status == load_status::COMPRESSED) {
    // nothing to do
  } else if (status == load_status::SWAPPED) {
    try {
      const io::storage_manager& sm = swap_information.storage_manager();
      if (sm.supports_storage() && !swap_file_exists(swap_file, swap_information)) {
        log::warn("could not find swap file {} for {} ", swap_file, desc);
        throw common::file_exception{"Could not find swap file: {}.", swap_file};
      }
      io::storage_manager::read_return_data<bool> read_result =
          sm.read_compressed_mt<bool>(swap_file, swap_information, size_on_disk);
      if (read_result.size != size) {
        log::warn(
            "broken swap file: size recorded in swap_file {} differs from size stored in memory: {} vs. {} for {}",
            swap_file, read_result.size, size.load(), desc);
        throw common::file_exception{
            "broken swap file: size recorded in swap_file [{}] differs from size stored in memory: [{}] vs. [{}].",
            swap_file, read_result.size, size.load()};
      }
      if (read_result.size > LARGE_SWAP_FILE_THRESHOLD) {
        log::jerror("Swapping in large file.",
                    to_json_swap_file_info(swap_information.get_swap_file_path(swap_file), description(),
                                           read_result.size, size_on_disk.load()));
        throw common::file_exception{"broken swap file: read invalid size [{}] from swap file [{}].", read_result.size,
                                     swap_file};
      }
      set_swap_in_read_size_tag(swap_context, read_result.size * sizeof(bool));
      auto new_data = memory::create_null_flags(read_result.size, context);
      for (size_t i = 0; i < read_result.size; i++) {
        (*new_data)[i] = read_result.data[static_cast<std::ptrdiff_t>(i)];
      }
      data = std::move(new_data);
      size = read_result.size;
      broken_swap_file = false;
    } catch (ctl::short_of_memory& e) {
      throw;
    } catch (ctl::bad_alloc& e) {
      throw;
    } catch (std::exception& e) {
      broken_swap_file = true;
      throw;
    }
  } else {
    throw common::internal_exception{"Unknown load status: [{}] at {}.", ctl::enum_to_underlying_type(status.load()),
                                     ctl::source_location{}};
  }

  status = load_status::LOADED;
  loaded_by = std::this_thread::get_id();
  loaded_at = mem_clock_t::now();

  log_warning_if_swap_in_too_long(timer_with_lock, timer_after_lock, description());
  add_swap_invocation_to_operator_statistics(swap_information.memory_manager(), BITSET_SWAP_IN_KEY,
                                             timer_after_lock.duration());
  return data;
#endif
}

#ifndef CELOSTAR
void swappable_bitset::swap_out_bitset(common::execution_context& context) {
  auto swap_out_context = create_swap_out_context(context, status.load(), swap_file, description(),
                                                  size.load() * sizeof(bool), size_on_disk.load(), 0);
  if (!persisted) {
    write_to_disk(swap_out_context);
    update_swap_file_disk_size_tag(swap_out_context, size_on_disk.load());
  }
  data.reset();
  status = load_status::SWAPPED;
  loaded_by = std::thread::id{};
  loaded_at = mem_time_t{};
}

void swappable_bitset::write_to_disk(common::execution_context& context) {
  common::timer timer;
  const io::storage_manager& sm = swap_information.storage_manager();
  if (!sm.supports_storage()) {
    return;
  }

  auto out_data{memory::tracking::make_static_array_for_overwrite<bool>(
      data->size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG), context)};
  for (size_t i = 0; i < data->size(); i++) {
    out_data[i] = (*data)[i];
  }

  io::byte_iterator iter{io::create_byte_iterator_from(std::span<const bool>(out_data))};
  size_on_disk = sm.compress_and_write_mt(swap_file, iter, sizeof(bool) * data->size(), swap_information,
                                          io::get_type_id<bool>(), context);
  persisted = true;
  broken_swap_file = false;
  timer.stop();
  add_swap_invocation_to_operator_statistics(swap_information.memory_manager(), BITSET_SWAP_OUT_KEY, timer.duration());
}
#endif

}  // namespace celonis::accelerator::memory::management
