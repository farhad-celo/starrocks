#pragma once

#include <memory>

#include "checked_ptr_fwd.h"
#include "legacy_embedded_ctl/bits/checked_ptr_utils.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename ValueType>
class checked_unique_ptr {
 public:
  using value_type = ValueType;
  using pointer_type = std::unique_ptr<value_type>;

  constexpr checked_unique_ptr() = default;

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_unique_ptr(std::nullptr_t /* nullptr */) {}

  template <typename T>
  constexpr explicit checked_unique_ptr(T* ptr) : ptr_{ptr} {}

  template <typename T>
  constexpr explicit checked_unique_ptr(checked_raw_ptr<T> ptr) : ptr_{ptr.get()} {}

  template <typename T, class DELETER>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_unique_ptr(std::unique_ptr<T, DELETER>&& ptr) : ptr_{std::move(ptr)} {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_unique_ptr(checked_unique_ptr<T>&& p) : ptr_{std::move(p.ptr_)} {}

  [[nodiscard]] value_type& operator*() const {
    details::check_not_null(*this);
    return *ptr_.get();
  }

  [[nodiscard]] value_type* operator->() const {
    details::check_not_null(*this);
    return ptr_.get();
  }

  [[nodiscard]] value_type* get() const noexcept { return ptr_.get(); }

  [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

  void reset() { ptr_.reset(); }

  template <typename T>
  void reset(T* const ptr) {
    ptr_.reset(ptr);
  }

  [[nodiscard]] const pointer_type& underlying() const { return ptr_; }

 private:
  template <typename T>
  friend class checked_unique_ptr;

  pointer_type ptr_{nullptr};
};

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& lhs, const checked_unique_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_unique_ptr<L>& lhs, const checked_unique_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& lhs, const std::unique_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_unique_ptr<L>& lhs, const std::unique_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& lhs, const checked_shared_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_unique_ptr<L>& lhs, const checked_shared_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& lhs, const std::shared_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_unique_ptr<L>& lhs, const std::shared_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& lhs, const R* rhs) noexcept {
  return lhs.get() == rhs;
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_unique_ptr<L>& lhs, const R* rhs) noexcept {
  return lhs.get() <=> rhs;
}

template <typename L>
[[nodiscard]] inline bool operator==(const checked_unique_ptr<L>& p, std::nullptr_t /*nullptr*/) noexcept {
  return p.get() == nullptr;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const checked_unique_ptr<T>& ptr) {
  return os << ptr.get();
}

template <typename T, typename... ARGS>
[[nodiscard]] checked_unique_ptr<T> make_checked_unique(ARGS&&... args) {
  return legacy_embedded_ctl::checked_unique_ptr<T>{std::make_unique<T>(std::forward<ARGS>(args)...)};
}

};  // namespace celonis::accelerator::legacy_embedded_ctl

namespace std {

template <typename T>
struct hash<celonis::accelerator::legacy_embedded_ctl::checked_unique_ptr<T>> {
  [[nodiscard]] constexpr size_t operator()(
      const celonis::accelerator::legacy_embedded_ctl::checked_unique_ptr<T>& p) const {
    return std::hash<T*>{}(p.get());
  }
};

};  // namespace std
