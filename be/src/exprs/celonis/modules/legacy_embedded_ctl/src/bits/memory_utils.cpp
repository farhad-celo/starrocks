#include "legacy_embedded_ctl/bits/memory_utils.h"

#include <queue>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bits/memory_utils.h"
#include "legacy_embedded_ctl/circular_buffer.h"
#include "legacy_embedded_ctl/memory/meminfo.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "legacy_embedded_ctl/utils/allocation_threshold.h"
#include "log/log.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {

struct allocation_meta_data final {
  std::uintptr_t address_{};                // 8B
  std::size_t array_size_in_bytes_{};       // 8B
  source_location source_code_location_{};  // 12B
  bool is_value_initialized_{};             // 1B
};

class n_largest {
 public:
  using entry_type = const allocation_meta_data*;
  using container_type = std::vector<entry_type>;

  struct greater_cmp {
    bool operator()(entry_type left, entry_type right) const {
      return left->array_size_in_bytes_ > right->array_size_in_bytes_;
    }
  };

  struct less_cmp {
    bool operator()(entry_type left, entry_type right) const {
      return left->array_size_in_bytes_ < right->array_size_in_bytes_;
    }
  };

  using priority_queue_type = std::priority_queue<entry_type, container_type, greater_cmp>;

  explicit n_largest(size_t n) : n_{n} {
    container_type container{};
    container.reserve(n);
    top_n_ = priority_queue_type{greater_cmp{}, std::move(container)};
  }

  void add(entry_type new_entry) {
    if (top_n_.size() < n_) {
      top_n_.push(new_entry);
      return;
    }

    entry_type smallest_of_top_n{top_n_.top()};
    if (less_cmp{}(smallest_of_top_n, new_entry)) {
      top_n_.pop();
      top_n_.push(new_entry);
    }
  }

  [[nodiscard]] container_type fetch_n_largest() noexcept {
    container_type container{};
    container.reserve(top_n_.size());
    while (!top_n_.empty()) {
      container.push_back(top_n_.top());
      top_n_.pop();
    }
    return container;
  }

 private:
  const size_t n_;
  priority_queue_type top_n_;
};

class allocation_tracker final {
 public:
  static allocation_tracker& instance() {
    static constexpr std::size_t NUMBER_OF_ALLOCATIONS_TO_TRACK{2000};
    static constexpr std::size_t NUMBER_OF_DEALLOCATIONS_TO_TRACK{2000};
    static allocation_tracker allocation_tracker{NUMBER_OF_ALLOCATIONS_TO_TRACK, NUMBER_OF_DEALLOCATIONS_TO_TRACK};
    return allocation_tracker;
  }

  void add_allocation(const std::uintptr_t allocation_address, const std::size_t allocation_size,
                      const source_location& allocation_location, const bool is_value_init) {
    tracked_allocations_.put(
        allocation_meta_data{allocation_address, allocation_size, allocation_location, is_value_init});
  }

  void add_deallocation(const std::uintptr_t allocation_address, const std::size_t deallocation_size,
                        const source_location& allocation_location, const bool is_value_init) {
    tracked_deallocations_.put(
        allocation_meta_data{allocation_address, deallocation_size, allocation_location, is_value_init});
  }

  [[nodiscard]] const circular_buffer<allocation_meta_data>& get_tracked_allocations() const noexcept {
    return tracked_allocations_;
  }

  [[nodiscard]] const circular_buffer<allocation_meta_data>& get_tracked_deallocations() const noexcept {
    return tracked_deallocations_;
  }

 private:
  explicit allocation_tracker(const std::size_t allocations_to_track, const std::size_t deallocations_to_track)
      : tracked_allocations_{allocations_to_track}, tracked_deallocations_{deallocations_to_track} {}
  circular_buffer<allocation_meta_data> tracked_allocations_;
  circular_buffer<allocation_meta_data> tracked_deallocations_;
};

}  // namespace

std::string build_string_containing_n_largest_open_allocations(size_t n) {
  n_largest n_largest_allocations_builder{n};
  const auto& tracked_allocations{allocation_tracker::instance().get_tracked_allocations()};
  const auto& tracked_deallocations{allocation_tracker::instance().get_tracked_deallocations()};

  tracked_allocations.apply([&n_largest_allocations_builder](const auto& span) {
    std::ranges::for_each(span, [&](const allocation_meta_data& alloc) { n_largest_allocations_builder.add(&alloc); });
  });

  const auto& n_largest_allocations{n_largest_allocations_builder.fetch_n_largest()};

  std::vector<const allocation_meta_data*> matching_deallocations{};
  matching_deallocations.reserve(n_largest_allocations.size());

  const auto deallocation_matches = [](const allocation_meta_data& alloc, const allocation_meta_data& dealloc) {
    if (alloc.address_ == dealloc.address_) {
      return alloc.source_code_location_.file_name() == dealloc.source_code_location_.file_name() &&
             alloc.source_code_location_.line() == dealloc.source_code_location_.line();
    }
    return false;
  };

  const auto find_matching_dealloc_in_span = [&](const std::span<allocation_meta_data>& span,
                                                 const allocation_meta_data& alloc) {
    const auto it{std::ranges::find_if(
        span, [&](const allocation_meta_data& dealloc) { return deallocation_matches(alloc, dealloc); })};
    return it == span.end() ? nullptr : std::to_address(it);
  };

  std::ranges::transform(
      n_largest_allocations, std::back_inserter(matching_deallocations), [&](const allocation_meta_data* alloc) {
        return tracked_deallocations.apply([alloc, &find_matching_dealloc_in_span](const auto& span) {
          return find_matching_dealloc_in_span(span, *alloc);
        });
      });

  std::string accumulated_msg{};
  std::ranges::for_each(n_largest_allocations, [&, i = 0](const allocation_meta_data* alloc) mutable {
    const auto* matching_dealloc{matching_deallocations[i]};
    i++;
    accumulated_msg +=
        fmt::format("Address: {}, Allocation Size: {}, Deallocation Size: {}, Source: {}:{}. ", alloc->address_,
                    alloc->array_size_in_bytes_,
                    matching_dealloc != nullptr ? std::to_string(matching_dealloc->array_size_in_bytes_) : "n/a",
                    alloc->source_code_location_.file_name_without_path(), alloc->source_code_location_.line());
  });
  return accumulated_msg;
}

namespace details {

void do_track_allocation(const std::uintptr_t allocation_address, const std::size_t allocation_size,
                         const source_location& allocation_location, const bool is_value_init) {
  allocation_tracker::instance().add_allocation(allocation_address, allocation_size, allocation_location,
                                                is_value_init);
}

void do_track_deallocation(std::uintptr_t allocation_address, std::size_t deallocation_size,
                           const source_location& allocation_location, bool is_value_init) {
  allocation_tracker::instance().add_deallocation(allocation_address, deallocation_size, allocation_location,
                                                  is_value_init);
}

void throw_if_not_enough_memory_for_allocation(const std::size_t bytes_to_allocate, std::string_view reason,
                                               const utils::allocation_priority priority) {
  if (priority == utils::allocation_priority::EMERGENCY) {
    return;
  }
  const auto threshold{allocation_priority_to_allocation_threshold(priority)};
  if (bytes_to_allocate != 0) {
    static constexpr auto BYTE_UNIT = legacy_embedded_ctl::byte_unit::KiB;
    const auto required_kib = bytes_to_allocate / 1_KiB;
    const auto mem_status{legacy_embedded_ctl::fetch_current_meminfo()};
    const auto mem_available_kib{mem_status.available<BYTE_UNIT>()};
    if (mem_available_kib < required_kib) {
      log::error("Not enough available free memory ({} KiB) for the allocation ('{}' priority) of {} KiB. {}",
                 mem_available_kib, priority, required_kib, reason);
      throw short_of_memory{};
    }
    const auto mem_total_kib{mem_status.total<BYTE_UNIT>()};
    if (static_cast<double>(mem_available_kib - required_kib) / static_cast<double>(mem_total_kib) < threshold) {
      log::error(
          "Allocation ('{}' priority) of {} KiB would lead to less than {}% free memory ({}/{}) available. Refusing to "
          "allocate memory. {}",
          priority, required_kib, threshold.as_percentage(), mem_available_kib - required_kib, mem_total_kib, reason);
      throw short_of_memory{};
    }

    // TODO(m.hueneberg): remove this when the memory estimation problem is fixed
    static std::atomic<bool> last_estimated_check_successful{true};

    auto& memory_consumption_tracker = global_memory_consumption_tracker::get_consumption_tracker();
    int64_t process_unmapped_estimated_kib =
        std::max<int64_t>(0, (static_cast<int64_t>(memory_consumption_tracker.static_process_size()) +
                              static_cast<int64_t>(memory_consumption_tracker.cur_net_allocated()) -
                              static_cast<int64_t>(mem_status.in_use_by_process<legacy_embedded_ctl::byte_unit::B>())) /
                                 static_cast<int64_t>(1_KiB));
    int64_t mem_estimated_available_kib = static_cast<int64_t>(mem_available_kib) - process_unmapped_estimated_kib;
    if (static_cast<double>(mem_estimated_available_kib - static_cast<int64_t>(required_kib)) /
            static_cast<double>(mem_total_kib) <
        threshold) {
      // Do not log if we already had the problem at the previous allocation. This should make sure that we do not get
      // an endless amount of logs, if the memory estimation ends up and remains in a bad state
      if (last_estimated_check_successful) {
        log_memory_tracking_state(
            mem_available_kib, mem_estimated_available_kib, mem_status.in_use_by_process<legacy_embedded_ctl::byte_unit::B>(),
            fmt::format("Rejecting allocation of size {} based on memory estimation.", bytes_to_allocate));
      }
      throw short_of_memory{true};
    }

    last_estimated_check_successful = true;
  }
}

void log_memory_tracking_state(size_t mem_available_kib, size_t mem_estimated_available_kib, size_t in_use_by_process,
                               const std::string& prefix) {
  auto& memory_consumption_tracker{global_memory_consumption_tracker::get_consumption_tracker()};
  log::jinfo(fmt::format("{} memory consumption tracker info", prefix),
             {{"real_available_mem_in_KiB", mem_available_kib},
              {"estimated_available_mem_in_KiB", mem_estimated_available_kib},
              {"static_process_size_in_B", memory_consumption_tracker.static_process_size()},
              {"in_use_by_process_in_B", in_use_by_process},
              {"estimated_net_allocated_in_B", memory_consumption_tracker.cur_net_allocated()},
              {"batched_tracker_count", memory_consumption_tracker.batched_tracker_count()},
              {"top_5_allocations:", build_string_containing_n_largest_open_allocations(5)}});
}

void log_large_allocation_warning(const std::size_t number_of_elements_to_allocate,
                                  const std::size_t byte_size_of_each_element,
                                  const utils::allocation_reason& allocation_reason) {
  const auto allocation_size_in_bytes{number_of_elements_to_allocate * byte_size_of_each_element};
  const auto allocation_size_in_gib{convert_byte_size<byte_unit::B, byte_unit::GiB>(allocation_size_in_bytes)};
  log::warn(
      "Allocate a suspiciously large chunk of memory: '{}' elements each with a size of '{}' bytes (in total '{}' "
      "GiB). {}",
      number_of_elements_to_allocate, byte_size_of_each_element, allocation_size_in_gib, allocation_reason.to_string());
}

}  // namespace details

}  // namespace celonis::accelerator::legacy_embedded_ctl
