#include "swappable_bitset.h"

#include <array>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include <tbb/parallel_for.h>

#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_utils.h"
#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "log/log.h"
#include "modules/common/aligned_blocked_range.h"
#include "modules/common/exceptions.h"
#ifndef CELOSTAR
#include "modules/io/storage_manager.h"
#include "modules/io/swap_data_types.h"
#endif
#include "modules/memory/management/const_bitset_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/raw_data_handler.h"
#ifndef CELOSTAR
#include "modules/memory/management/swap_context.h"
#endif
#include "modules/memory/management/swap_info.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"

namespace celonis::accelerator::memory::management {

namespace {

// Transform a dynamic_bitset to an array of 64-bit ints.
// The last element in the array indicates the number of bits,
// the rest is the bitset data itself.
[[nodiscard]] legacy_embedded_ctl::static_array<uint64_t> copy_to_static_array(const legacy_embedded_ctl::dynamic_bitset_t& data) {
  auto array{legacy_embedded_ctl::make_static_array_for_overwrite<uint64_t>(static_cast<size_t>(data.num_blocks()) + 1,
                                                            LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG))};
  auto* array_last{std::copy(data.data(), data.data() + data.num_blocks(), array.begin())};
  *array_last = data.size();
  return array;
}

#ifndef CELOSTAR
// Transform a boolean array to an uint64_t array.
// The last element in the array indicates the number of bits,
// the rest is the bitset data itself.
[[nodiscard]] legacy_embedded_ctl::static_array<uint64_t> copy_to_static_array(const const_data_accessor<bool>& data) {
  auto array{legacy_embedded_ctl::make_static_array_for_overwrite<uint64_t>(
      static_cast<size_t>(legacy_embedded_ctl::details::calc_number_of_bitset_blocks(data.size())) + 1,
      LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG))};
  array.back() = data.size();
  legacy_embedded_ctl::bitset_mutable_view_t view{legacy_embedded_ctl::array_view<uint64_t>{array.begin(), array.end() - 1}, data.size()};

  tbb::parallel_for(common::safe_aligned_blocked_range<size_t>{0, data.size()},
                    [&view, &data = std::as_const(data)](const auto r) {
                      for (auto i = r.begin; i < r.end; i++) {
                        view.set(i, data[i]);
                      }
                    });
  return array;
}
#endif
}  // namespace

std::shared_ptr<swappable_bitset> swappable_bitset::create_data_handler(const memory::null_flags_t& data,
                                                                        const std::string& swap_file,
                                                                        const swap_info& sinfo,
                                                                        const std::string& description) {
  legacy_embedded_debug_assert(sinfo.is_no_swap() || swap_file.ends_with(NULL_FLAGS_ENDING) ||
               swap_file.ends_with(NULL_FLAGS_NEW_ENDING));
  auto data_handler_data{copy_to_static_array(*data)};
  const std::string swap_file_bs_ending{swap_file.substr(0, swap_file.find_last_of('.')) + BITSET_ENDING};
  auto data_handler{raw_data_handler<uint64_t>::create_data_handler(std::move(data_handler_data), swap_file_bs_ending,
                                                                    sinfo, description)};
  std::shared_ptr<swappable_bitset> raw_data(
      new swappable_bitset(std::move(data_handler), swap_file, sinfo, data->size()));
  return raw_data;
}

#ifndef CELOSTAR
std::shared_ptr<swappable_bitset> swappable_bitset::init_from_swap(const std::string& swap_file, const swap_info& sinfo,
                                                                   const std::string& description) {
  legacy_embedded_debug_assert(sinfo.is_no_swap() || swap_file.ends_with(NULL_FLAGS_ENDING) ||
               swap_file.ends_with(NULL_FLAGS_NEW_ENDING) || swap_file.ends_with(BITSET_ENDING));
  std::shared_ptr<swappable_bitset> ret{nullptr};
  if (swap_file.ends_with(NULL_FLAGS_ENDING) || swap_file.ends_with(NULL_FLAGS_NEW_ENDING)) {
    ret = init_from_swap_byte_array(swap_file, sinfo, description);
  }

  if (ret == nullptr) {
    // Currently, the engine still reads from NULL_FLAGS_ENDING/NULL_FLAGS_NEW_ENDING swap files, but
    // when it fails, it tries also to read from BITSET_ENDING. This makes the current engine BITSET_ENDING compatible.
    const std::string swap_file_bs_ending{swap_file.substr(0, swap_file.find_last_of('.')) + BITSET_ENDING};
    ret = init_from_swap_bitset(swap_file_bs_ending, sinfo, description);
  }

  return ret;
}

std::shared_ptr<swappable_bitset> swappable_bitset::init_from_swap_byte_array(const std::string& swap_file,
                                                                              const swap_info& sinfo,
                                                                              const std::string& description) {
  legacy_embedded_debug_assert(sinfo.is_no_swap() || swap_file.ends_with(NULL_FLAGS_ENDING) ||
               swap_file.ends_with(NULL_FLAGS_NEW_ENDING));
  auto bool_data_handler{raw_data_handler<bool>::init_from_swap(swap_file, sinfo, description)};
  if (bool_data_handler != nullptr) {
    auto data{copy_to_static_array(bool_data_handler->get_const_data())};
    const std::string swap_file_bs_ending{swap_file.substr(0, swap_file.find_last_of('.')) + BITSET_ENDING};
    auto data_handler{
        raw_data_handler<uint64_t>::create_data_handler(std::move(data), swap_file_bs_ending, sinfo, description)};
    std::shared_ptr<swappable_bitset> raw_data(
        new swappable_bitset(std::move(data_handler), swap_file, sinfo, bool_data_handler->get_size()));
    return raw_data;
  }
  return {};
}

std::shared_ptr<swappable_bitset> swappable_bitset::init_from_swap_bitset(const std::string& swap_file,
                                                                          const swap_info& sinfo,
                                                                          const std::string& description) {
  legacy_embedded_debug_assert(sinfo.is_no_swap() || swap_file.ends_with(BITSET_ENDING));
  auto data_handler{raw_data_handler<uint64_t>::init_from_swap(swap_file, sinfo, description)};
  if (data_handler == nullptr) {
    return {};
  }

  const std::string swap_file_nf_ending{swap_file.substr(0, swap_file.find_last_of('.')) + NULL_FLAGS_ENDING};
  std::shared_ptr<swappable_bitset> raw_data(new swappable_bitset(std::move(data_handler), swap_file_nf_ending, sinfo));
  return raw_data;
}
#endif

load_status swappable_bitset::get_load_status() const { return data_handler_->get_load_status(); }

persistence_status swappable_bitset::get_persistence_status() const { return data_handler_->get_persistence_status(); }

bool swappable_bitset::swap_file_broken() const { return data_handler_->swap_file_broken(); }

#ifndef CELOSTAR
bool swappable_bitset::swap_out(common::execution_context& context) {
  if (persisted_) {
    return persisted_;
  }

  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex_, LOCK_LOGGING_THRESHOLD)};

  if (persisted_) {
    return persisted_;
  }

  write_null_flag_ending_to_disk(context);
  swap_out_bitset_ending(context);
  return persisted_;
}

void swappable_bitset::write_out(common::execution_context& context) {
  const auto data_mutex_unique_lock{concurrency::lock_with_logging(data_mutex_, LOCK_LOGGING_THRESHOLD)};
  write_null_flag_ending_to_disk(context);
  write_out_bitset_ending(context);
}

void swappable_bitset::write_null_flag_ending_to_disk(common::execution_context& context) {
  if (!sinfo_.is_swappable()) {
    return;
  }

  const auto uint64_array{data_handler_->get_const_data(context).shared()};

  legacy_embedded_ctl::static_array<bool> out_data{};
  if (!uint64_array.empty()) {
    const size_t size{uint64_array.back()};
    out_data =
        memory::tracking::make_static_array_value_init<bool>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context);
    const legacy_embedded_ctl::bitset_view_t view{legacy_embedded_ctl::array_view<const uint64_t>{uint64_array.begin(), uint64_array.end() - 1}, size};
    view.apply_in_range([&out_data](const size_t i) { out_data[i] = true; });
  }

  auto bool_data_handler{
      raw_data_handler<bool>::create_data_handler(std::move(out_data), swap_file_, sinfo_, description())};

  bool_data_handler->swap_out(context);
  persisted_ = true;
}

bool swappable_bitset::swap_out_bitset_ending(common::execution_context& context) {
  return data_handler_->swap_out(context);
}

void swappable_bitset::write_out_bitset_ending(common::execution_context& context) {
  data_handler_->write_out(context);
}
#endif

swappable_bitset::const_data_accessor_t swappable_bitset::get_const_data(
    [[maybe_unused]] const common::execution_context& context) {
  const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex_, LOCK_LOGGING_THRESHOLD)};
  auto data_handler_data{data_handler_->get_const_data(context).shared()};
  legacy_embedded_debug_assert(!data_handler_data.empty());
  const legacy_embedded_ctl::bitset_view_t view{legacy_embedded_ctl::array_view<const uint64_t>{data_handler_data.begin(), data_handler_data.end() - 1},
                                data_handler_data.back()};
  return swappable_bitset::const_data_accessor_t{view, std::move(data_handler_data)};
}

bool swappable_bitset::is_swappable() const { return data_handler_->is_swappable(); }

#ifndef CELOSTAR
bool swappable_bitset::compress() { return true; };
#endif

size_t swappable_bitset::get_size_in_memory() const { return data_handler_->get_size_in_memory(); };

size_t swappable_bitset::get_size() const { return size_; }

size_t swappable_bitset::get_size_on_disk() const { return size_on_disk_ + data_handler_->get_size_on_disk(); }
size_t swappable_bitset::get_usage_count() const { return data_handler_->get_usage_count(); }

usage_time_t swappable_bitset::get_last_usage() const { return data_handler_->get_last_usage(); }

std::thread::id swappable_bitset::get_loaded_by() const { return data_handler_->get_loaded_by(); }

[[nodiscard]] load_time_t swappable_bitset::get_loaded_at() const { return data_handler_->get_loaded_at(); }

std::string swappable_bitset::description() const { return data_handler_->description(); }

void swappable_bitset::swap_in(const common::execution_context& context) { data_handler_->swap_in(context); }

bool swappable_bitset::is_persisted() const { return persisted_; }

void swappable_bitset::set_delete_from_disk_when_destructed(const bool value) {
  delete_from_disk_when_destructed_ = value;
  data_handler_->set_delete_from_disk_when_destructed(value);
}

swappable_bitset::~swappable_bitset() {
#ifndef CELOSTAR
  // TODO (j.boettcher) This destructor can be deleted once we stop the support of the legacy (boolean) swap format
  if (sinfo_.is_swappable()) {
    if (persisted_ && delete_from_disk_when_destructed_.load()) {
      const io::storage_manager& sm{sinfo_.storage_manager()};
      try {
        sm.delete_swap_file(swap_file_, sinfo_);
      } catch (const std::exception& e) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler. Reason: [{}].", swap_file_,
                  e.what());
      } catch (...) {
        log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler.", swap_file_);
      }
    }
  }
#endif
}

swappable_bitset::swappable_bitset(memory::management::raw_data_handler_t<uint64_t>&& data_handler,
                                   std::string swap_file, swap_info sinfo, std::optional<size_t> size)
    : data_handler_{std::move(data_handler)}, swap_file_{std::move(swap_file)}, sinfo_{std::move(sinfo)} {
  common::execution_context context;
  // if this constructor is called from init_from_swap, we read it once directly in order to read the size
  // of the bits, which is stored as the last uint64_t element in the data.
  size_ = size.has_value() ? size.value() : get_const_data(context).size();
}

}  // namespace celonis::accelerator::memory::management
