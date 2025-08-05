#pragma once

#include <memory>

#include "checked_ptr_fwd.h"
#include "checked_shared_ptr.h"
#include "checked_unique_ptr.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename ValueType>
class checked_raw_ptr {
 public:
  using value_type = ValueType;
  using pointer_type = ValueType*;

  constexpr checked_raw_ptr() = default;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_raw_ptr(checked_raw_ptr<T> ptr) : ptr_{ptr.get()} {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_raw_ptr(std::nullptr_t /* nullptr */) {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr checked_raw_ptr(T* ptr) : ptr_{ptr} {}

  [[nodiscard]] value_type& operator*() const {
    details::check_not_null(*this);
    return *ptr_;
  }

  [[nodiscard]] value_type* operator->() const {
    details::check_not_null(*this);
    return ptr_;
  }

  [[nodiscard]] value_type* get() const noexcept { return ptr_; }

  [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

 private:
  pointer_type ptr_{nullptr};
};

template <typename To, typename From>
[[nodiscard]] checked_raw_ptr<To> static_pointer_cast(const checked_raw_ptr<From>& ptr) {
  return {static_cast<To*>(ptr.get())};
}

template <typename To, typename From>
[[nodiscard]] checked_raw_ptr<To> const_pointer_cast(const checked_raw_ptr<From>& ptr) {
  return {const_cast<To*>(ptr.get())};  // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const checked_raw_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const checked_raw_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const R* rhs) noexcept {
  return lhs.get() == rhs;
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const R* rhs) noexcept {
  return lhs.get() <=> rhs;
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const checked_unique_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const checked_unique_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const std::unique_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const std::unique_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const checked_shared_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const checked_shared_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& lhs, const std::shared_ptr<R>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename L, typename R>
[[nodiscard]] inline auto operator<=>(const checked_raw_ptr<L>& lhs, const std::shared_ptr<R>& rhs) noexcept {
  return lhs.get() <=> rhs.get();
}

template <typename L>
[[nodiscard]] inline bool operator==(const checked_raw_ptr<L>& p, std::nullptr_t /*nullptr*/) noexcept {
  return p.get() == nullptr;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const checked_raw_ptr<T>& ptr) {
  return os << ptr.get();
}

};  // namespace celonis::accelerator::legacy_embedded_ctl

namespace std {

template <typename T>
struct hash<celonis::accelerator::legacy_embedded_ctl::checked_raw_ptr<T>> {
  [[nodiscard]] constexpr size_t operator()(const celonis::accelerator::legacy_embedded_ctl::checked_raw_ptr<T>& p) const {
    return std::hash<T*>{}(p.get());
  }
};

};  // namespace std
