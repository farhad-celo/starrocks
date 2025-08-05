#pragma once

#include <memory>

#include "checked_ptr_fwd.h"
#include "checked_shared_ptr.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename VALUE_TYPE>
class checked_weak_ptr {
  using value_type = VALUE_TYPE;
  using pointer_type = std::weak_ptr<VALUE_TYPE>;

 public:
  constexpr checked_weak_ptr() = default;

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_weak_ptr(std::nullptr_t /* nullptr */) {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_weak_ptr(std::weak_ptr<T> ptr) : ptr_{std::move(ptr)} {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_weak_ptr(checked_weak_ptr<T> p) : ptr_{std::move(p.ptr_)} {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_weak_ptr(std::shared_ptr<T> ptr) : ptr_{ptr} {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_weak_ptr(checked_shared_ptr<T> p) : ptr_{p.underlying()} {}

  [[nodiscard]] auto use_count() const { return ptr_.use_count(); }

  [[nodiscard]] bool expired() const { return ptr_.expired(); }

  [[nodiscard]] checked_shared_ptr<value_type> lock() const { return {ptr_.lock()}; }

  void reset() { ptr_.reset(); }

  [[nodiscard]] const pointer_type& underlying() const { return ptr_; }

 private:
  template <typename T>
  friend class checked_weak_ptr;

  pointer_type ptr_{};
};

template <typename To, typename From>
[[nodiscard]] checked_weak_ptr<To> static_pointer_cast(const checked_weak_ptr<From>& ptr) {
  return {static_pointer_cast<To>(ptr.lock())};
}

template <typename To, typename From>
[[nodiscard]] checked_weak_ptr<To> const_pointer_cast(const checked_weak_ptr<From>& ptr) {
  return {const_pointer_cast<To>(ptr.lock())};
}

};  // namespace celonis::accelerator::legacy_embedded_ctl
