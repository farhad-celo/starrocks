#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

#include "legacy_embedded_ctl/memory/meminfo.h"

namespace celonis::accelerator::legacy_embedded_ctl {

void global_memory_consumption_tracker::register_query() noexcept {
  std::unique_lock lck{update_lock};
  if (active_query_count_++ == 0) {
    try {
      auto cur_meminfo{legacy_embedded_ctl::fetch_current_meminfo()};
      std::size_t last_process_rss_size_{cur_meminfo.in_use_by_process<legacy_embedded_ctl::byte_unit::B>()};
      static_process_size_ = last_process_rss_size_ - net_allocated_;
    } catch (...) {
      // if fetching meminfo fails, just continue with defaults that will not cause rejections
      static_process_size_ = 0;
    }
  }
}

void global_memory_consumption_tracker::deregister_query() noexcept { active_query_count_--; }

size_t global_memory_consumption_tracker::static_process_size() const noexcept { return static_process_size_; }

size_t global_memory_consumption_tracker::cur_net_allocated() const noexcept { return net_allocated_; }

size_t global_memory_consumption_tracker::cur_metadata_allocated() const noexcept { return metadata_allocated_; }

void global_memory_consumption_tracker::register_allocation(const std::size_t size, const bool as_metadata) noexcept {
  net_allocated_ += size;
  if (as_metadata) {
    metadata_allocated_ += size;
  }
}

void global_memory_consumption_tracker::deregister_allocation(const std::size_t size, const bool as_metadata) noexcept {
  net_allocated_ -= size;
  if (as_metadata) {
    metadata_allocated_ -= size;
  }
}

void global_memory_consumption_tracker::register_batched_tracker() noexcept {
  batched_tracker_count_.fetch_add(1, std::memory_order_relaxed);
}

void global_memory_consumption_tracker::deregister_batched_tracker() noexcept {
  batched_tracker_count_.fetch_add(-1, std::memory_order_relaxed);
}

size_t global_memory_consumption_tracker::batched_tracker_count() noexcept {
  return batched_tracker_count_.load(std::memory_order_relaxed);
}

size_t global_memory_consumption_tracker::active_query_count() const noexcept { return active_query_count_; }

global_memory_consumption_tracker& global_memory_consumption_tracker::get_consumption_tracker() noexcept {
  static global_memory_consumption_tracker memory_consumption_tracker_;
  return memory_consumption_tracker_;
}

scoped_query_registerer::scoped_query_registerer() {
  global_memory_consumption_tracker::get_consumption_tracker().register_query();
}

scoped_query_registerer::~scoped_query_registerer() {
  global_memory_consumption_tracker::get_consumption_tracker().deregister_query();
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
