#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>

/**
 * @brief very minimal version of a circular buffer (current use case is for storing allocation meta data only)
 */
namespace celonis::accelerator::legacy_embedded_ctl {

template <typename T>
class circular_buffer final {
 public:
  using size_type = std::size_t;
  using value_type = T;

  explicit circular_buffer(size_type size);
  void put(const value_type& value);
  // currently for testing only
  [[nodiscard]] const value_type& operator[](size_type idx) const noexcept;
  [[nodiscard]] size_type size() const noexcept;

  template <typename F>
  requires std::invocable<F, std::span<value_type>>
  [[nodiscard]] auto apply(F&& f) const noexcept {
    std::shared_lock lock{buffer_mutex_};
    return f(std::span<value_type>{buffer_.get(), actual_size_});
  }

 private:
  mutable std::shared_mutex buffer_mutex_{};
  size_type next_position_{0};
  size_type size_;
  size_type actual_size_{0};
  std::unique_ptr<T[]> buffer_;  // cannot use static_array/tracked alloc here because of dependency cycle
};

template <typename T>
circular_buffer<T>::circular_buffer(const size_type size) : size_{size}, buffer_{std::make_unique<T[]>(size_)} {}

template <typename T>
void circular_buffer<T>::put(const T& value) {
  std::unique_lock lock{buffer_mutex_};
  buffer_[next_position_++] = value;
  next_position_ %= size_;
  if (actual_size_ < size_) {
    actual_size_++;
  }
}

template <typename T>
typename circular_buffer<T>::size_type circular_buffer<T>::size() const noexcept {
  std::shared_lock lock{buffer_mutex_};
  return actual_size_;
}

template <typename T>
const typename circular_buffer<T>::value_type& circular_buffer<T>::operator[](size_type idx) const noexcept {
  std::shared_lock lock{buffer_mutex_};
  return buffer_[idx % size_];
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
