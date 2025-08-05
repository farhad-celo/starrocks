#include "legacy_embedded_ctl/memory/checked_memory_resource.h"

#include "legacy_embedded_ctl/bits/memory_utils.h"

namespace celonis::accelerator::legacy_embedded_ctl {

checked_memory_resource::checked_memory_resource(const utils::allocation_reason& reason,
                                                 utils::allocation_priority priority, size_t min_bytes_for_check,
                                                 bool using_value_init, abstract_resource_t upstream_resource) noexcept
    : memory_resource_with_upstream_resource{std::move(upstream_resource)},
      allocation_reason_{reason.to_string()},
      source_location_{reason.source_location()},
      priority_{priority},
      min_bytes_for_check_{min_bytes_for_check},
      using_value_init_{using_value_init} {}

void* checked_memory_resource::do_allocate(const std::size_t bytes, const std::size_t alignment) {
  auto upstream_alloc = [this, bytes, alignment]() {
    return get_upstream_memory_resource()->allocate(bytes, alignment);
  };

  if (bytes >= min_bytes_for_check_) {
    details::throw_if_not_enough_memory_for_allocation(bytes, allocation_reason_, priority_);

    auto deleter = [this, bytes, alignment](auto* p) {
      get_upstream_memory_resource()->deallocate(p, bytes, alignment);
    };
    std::unique_ptr<void, decltype(deleter)> alloc{upstream_alloc(), deleter};
    details::do_track_allocation(reinterpret_cast<std::uintptr_t>(alloc.get()), bytes, source_location_,
                                 using_value_init_);
    return alloc.release();
  }

  return upstream_alloc();
}

void checked_memory_resource::do_deallocate(void* p, const std::size_t bytes, const std::size_t alignment) {
  get_upstream_memory_resource()->deallocate(p, bytes, alignment);
  if (bytes >= min_bytes_for_check_) {
    details::do_track_deallocation(reinterpret_cast<std::uintptr_t>(p), bytes, source_location_, using_value_init_);
  }
}

bool checked_memory_resource::do_is_equal(const abstract_resource_base& other) const noexcept {
  const auto* const other_checked_resource{dynamic_cast<const checked_memory_resource*>(std::addressof(other))};
  if (other_checked_resource == nullptr) {
    return false;
  }
  bool upstream_resources_equal{
      get_upstream_memory_resource()->is_equal(*other_checked_resource->get_upstream_memory_resource())};
  bool source_locations_equal{
      source_location_.line() == other_checked_resource->source_location_.line() &&
      std::strcmp(source_location_.file_name(), other_checked_resource->source_location_.file_name()) == 0};
  bool other_members_equal{
      std::tie(using_value_init_, min_bytes_for_check_) ==
      std::tie(other_checked_resource->using_value_init_, other_checked_resource->min_bytes_for_check_)};

  return source_locations_equal && other_members_equal && upstream_resources_equal;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl