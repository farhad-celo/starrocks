#pragma once

#include <mutex>
#include <shared_mutex>
#include <type_traits>

#include "legacy_embedded_ctl/exception.h"

namespace celonis::accelerator::legacy_embedded_ctl {

class null_mutex final {
 public:
  static constexpr void lock() noexcept {}
  static constexpr void unlock() noexcept {}
  [[nodiscard]] static constexpr bool try_lock() noexcept { return true; }
};

/**
 * @brief A utility class which owns a resource and provides access to it in a thread-safe manner.
 * @description owning_mutex is constructed by providing a resource type as the template parameter, and the arguments
 * to construct the resource from. The arguments are forwarded to the constructor of the resource. The member functions
 * of owning_mutex take a function with a single parameter and invoke the function with the owned resource as argument.
 * The mutex is locked before the function is invoked and is freed after it returns. By this means, the access control
 * logic to the resource need not to be implemented within the functions, and is managed by the owning_mutex.
 * @tparam RESOURCE_TYPE type of the resource owned by owning_mutex
 */
template <typename RESOURCE_TYPE>
class owning_mutex {
 public:
  owning_mutex() = default;
  template <typename... ARGS>
  requires std::constructible_from<RESOURCE_TYPE, ARGS...>
  explicit owning_mutex(std::in_place_t /* in_place_t */, ARGS&&... args) : data_{std::forward<ARGS>(args)...} {}

  template <typename T = RESOURCE_TYPE>
  explicit owning_mutex(T&& resource) : data_{std::forward<T>(resource)} {}

  template <typename T, typename... ARGS>
  explicit owning_mutex(std::in_place_t /* in_place_t */, std::initializer_list<T> ilist, ARGS&&... args)
      : data_{ilist, std::forward<ARGS>(args)...} {}

  template <typename FUNCTION>
  decltype(auto) lock_mutable(const FUNCTION& fn) {
    std::lock_guard lock{mutex_};
    return fn(data_);
  }

  template <typename FUNCTION, class REP, class PERIOD>
  decltype(auto) try_lock_mutable_for(std::chrono::duration<REP, PERIOD> timeout, const FUNCTION& fn) {
    std::unique_lock lck{mutex_, timeout};
    if (!lck.owns_lock()) {
      throw timeout_exception{"Acquiring locked unique ownership failed."};
    }
    return fn(data_);
  }

  template <typename FUNCTION>
  decltype(auto) lock_shared(const FUNCTION& fn) const {
    std::shared_lock lock{mutex_};
    return fn(data_);
  }

 private:
  RESOURCE_TYPE data_{};
  mutable std::shared_timed_mutex mutex_{};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
