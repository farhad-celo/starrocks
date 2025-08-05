#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <type_traits>

#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

namespace celonis::accelerator::legacy_embedded_ctl {
template <class T, typename ALLOCATOR_TYPE>
class deregistering_deleter {
 public:
  explicit deregistering_deleter(const std::size_t size, ALLOCATOR_TYPE allocator) noexcept
      : size_{size}, allocator_{std::move(allocator)} {}

  void operator()(T ptr) {
    if constexpr (!std::is_trivially_destructible_v<std::remove_extent_t<T>>) {
      std::for_each(ptr, std::next(ptr, size_), [this](auto& element) {
        std::allocator_traits<ALLOCATOR_TYPE>::destroy(allocator_, std::addressof(element));
      });
    }
    std::allocator_traits<ALLOCATOR_TYPE>::deallocate(allocator_, ptr, size_);
  }

  [[nodiscard]] ALLOCATOR_TYPE get_allocator() const noexcept { return allocator_; }

 private:
  std::size_t size_{};
  ALLOCATOR_TYPE allocator_;
};

template <class T, typename ALLOCATOR_TYPE>
using deregistering_unique_ptr = std::unique_ptr<T, deregistering_deleter<T, ALLOCATOR_TYPE>>;

template <class T, typename ALLOCATOR_TYPE>
deregistering_unique_ptr<T, ALLOCATOR_TYPE> make_deregistering_unique(std::remove_extent_t<T>* alloc_ptr,
                                                                      const std::size_t alloc_size,
                                                                      ALLOCATOR_TYPE allocator) noexcept {
  return deregistering_unique_ptr<T, ALLOCATOR_TYPE>{
      alloc_ptr, deregistering_deleter<T, ALLOCATOR_TYPE>{alloc_size, std::move(allocator)}};
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
