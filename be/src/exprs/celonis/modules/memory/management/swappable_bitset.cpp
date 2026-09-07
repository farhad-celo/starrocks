#include "swappable_bitset.h"

#include <memory>
#include <utility>

#include <ctl/array_view.h>
#include <ctl/bitset.h>
#include <ctl/static_array.h>

#include "concurrency/concurrency_utils.h"
#include "log/log.h"
#include "modules/memory/management/const_bitset_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/raw_data_handler.h"

namespace celonis::accelerator::memory::management {

namespace {

// Transform a dynamic_bitset to an array of 64-bit ints.
// The last element in the array indicates the number of bits,
// the rest is the bitset data itself.
[[nodiscard]] ctl::static_array<uint64_t> copy_to_static_array(const ctl::dynamic_bitset_t& data) {
  auto array{ctl::make_static_array_for_overwrite<uint64_t>(static_cast<size_t>(data.num_blocks()) + 1,
                                                            ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
  auto* array_last{std::copy(data.data(), data.data() + data.num_blocks(), array.begin())};
  *array_last = data.size();
  return array;
}

}  // namespace

std::shared_ptr<swappable_bitset> swappable_bitset::create_data_handler(const memory::null_flags_t& data,
                                                                        const std::string& description) {
  auto data_handler_data{copy_to_static_array(*data)};
  auto data_handler{raw_data_handler<uint64_t>::create_data_handler(std::move(data_handler_data), description)};
  std::shared_ptr<swappable_bitset> raw_data(new swappable_bitset(std::move(data_handler), data->size()));
  return raw_data;
}

load_status swappable_bitset::get_load_status() const { return data_handler_->get_load_status(); }

swappable_bitset::const_data_accessor_t swappable_bitset::get_const_data(
    [[maybe_unused]] const common::execution_context& context) {
  const auto data_mutex_lock{concurrency::lock_shared_with_logging(data_mutex_, LOCK_LOGGING_THRESHOLD)};
  auto data_handler_data{data_handler_->get_const_data(context).shared()};
  debug_assert(!data_handler_data.empty());
  const ctl::bitset_view_t view{ctl::array_view<const uint64_t>{data_handler_data.begin(), data_handler_data.end() - 1},
                                data_handler_data.back()};
  return swappable_bitset::const_data_accessor_t{view, std::move(data_handler_data)};
}

size_t swappable_bitset::get_size_in_memory() const { return data_handler_->get_size_in_memory(); };

size_t swappable_bitset::get_size() const { return size_; }

size_t swappable_bitset::get_usage_count() const { return data_handler_->get_usage_count(); }

usage_time_t swappable_bitset::get_last_usage() const { return data_handler_->get_last_usage(); }

std::thread::id swappable_bitset::get_loaded_by() const { return data_handler_->get_loaded_by(); }

[[nodiscard]] load_time_t swappable_bitset::get_loaded_at() const { return data_handler_->get_loaded_at(); }

swappable_bitset::~swappable_bitset() = default;

swappable_bitset::swappable_bitset(memory::management::raw_data_handler_t<uint64_t>&& data_handler,
                                   std::optional<size_t> size)
    : data_handler_{std::move(data_handler)} {
  common::execution_context context;
  // if this constructor is called from init_from_swap, we read it once directly in order to read the size
  // of the bits, which is stored as the last uint64_t element in the data.
  size_ = size.has_value() ? size.value() : get_const_data(context).size();
}

}  // namespace celonis::accelerator::memory::management
