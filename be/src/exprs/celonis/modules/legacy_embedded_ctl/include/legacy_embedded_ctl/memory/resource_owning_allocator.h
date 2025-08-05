#pragma once

#include <memory>
#include <utility>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * The main difference to std::pmr::polymorphic_allocator is that we own the upstream memory_resource
 */
template <typename T>
class resource_owning_allocator {
 public:
  using value_type = T;

  // TODO(a.swoboda) when we have compiler support, switch to std::pmr::memory_resource
  using memory_resource_type = boost::container::pmr::memory_resource;

  // Allow implicit conversion of memory resource, same as in std::pmr
  // NOLINTNEXTLINE(google-explicit-constructor)
  resource_owning_allocator(std::shared_ptr<memory_resource_type> resource = nullptr) noexcept
      : memory_resource_{std::move(resource)} {
    if (!memory_resource_) {
      // use the default memory resource
      // NB the default memory resource is not owned, so we need to alias it with an empty shared_ptr.
      //  We could also pass an empty deleter in the constructor, but by aliasing, we avoid the control block entirely.
      memory_resource_ = std::shared_ptr<memory_resource_type>{std::move(memory_resource_),
                                                               boost::container::pmr::get_default_resource()};
    }
  }

  // Allow implicit conversion in converting constructor, same as in std::pmr
  template <typename U>  // NOLINTNEXTLINE(google-explicit-constructor)
  resource_owning_allocator(const resource_owning_allocator<U>& other) noexcept
      : memory_resource_{other.memory_resource_} {}

  // We default these because of the rule of 5. We are actually only interested in providing a custom move-constructor
  // and move-assignment implementation.
  ~resource_owning_allocator() noexcept = default;
  resource_owning_allocator(const resource_owning_allocator& other) noexcept = default;
  resource_owning_allocator& operator=(const resource_owning_allocator& other) noexcept = default;

  // We copy the shared ptr instead of moving it to make this play nicely with allocator propagation. A moved-from
  // allocator should still provide a working comparison- and allocate operator.
  // NOLINTNEXTLINE(performance-move-constructor-init)
  resource_owning_allocator(resource_owning_allocator&& other) noexcept : resource_owning_allocator{other} {}
  // NOLINTNEXTLINE(misc-unconventional-assign-operator)
  resource_owning_allocator& operator=(resource_owning_allocator&& other) noexcept { return *this = other; }

  [[nodiscard]] T* allocate(std::size_t n) const {
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    return static_cast<T*>(memory_resource_->allocate(n * sizeof(T), alignof(T)));
  }
  void deallocate(T* ptr, std::size_t n) const {
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    memory_resource_->deallocate(ptr, n * sizeof(T), alignof(T));
  }

  template <typename U>
  [[nodiscard]] bool operator==(const resource_owning_allocator<U>& rhs) const noexcept {
    return memory_resource_ == rhs.memory_resource_ || memory_resource_->is_equal(*rhs.memory_resource_);
  }
  template <typename U>
  [[nodiscard]] bool operator!=(const resource_owning_allocator<U>& rhs) const noexcept {
    return !operator==(rhs);
  }

 private:
  template <typename U>
  friend class resource_owning_allocator;

  std::shared_ptr<memory_resource_type> memory_resource_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
