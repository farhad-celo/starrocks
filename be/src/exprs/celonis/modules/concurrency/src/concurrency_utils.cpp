#include "concurrency/concurrency_utils.h"

#include <algorithm>
#include <thread>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/common/timer.h"

namespace celonis::accelerator::concurrency {

unsigned int hardware_concurrency() noexcept { return std::max<unsigned int>(1, std::thread::hardware_concurrency()); }

namespace {

template <typename MUTEX>
inline std::unique_lock<MUTEX> lock_validated_internal(MUTEX& mutex, const std::chrono::seconds& duration,
                                                       legacy_embedded_ctl::source_location source_location) {
  std::unique_lock lck{mutex, duration};
  if (!lck.owns_lock()) {
    throw common::internal_exception{"Acquiring lock failed at {}.", source_location};
  }
  return lck;
}

template <typename MUTEX>
std::shared_lock<MUTEX> lock_shared_validated_internal(MUTEX& mutex, const std::chrono::seconds& duration,
                                                       legacy_embedded_ctl::source_location source_location) {
  std::shared_lock lck{mutex, duration};
  if (!lck.owns_lock()) {
    throw common::internal_exception{"Acquiring lock failed at {}.", source_location};
  }
  return lck;
}

}  // anonymous namespace

std::unique_lock<std::shared_timed_mutex> lock_validated(std::shared_timed_mutex& mutex,
                                                         const std::chrono::seconds& duration,
                                                         legacy_embedded_ctl::source_location source_location) {
  return lock_validated_internal(mutex, duration, source_location);
}

std::unique_lock<std::timed_mutex> lock_validated(std::timed_mutex& mutex, const std::chrono::seconds& duration,
                                                  legacy_embedded_ctl::source_location source_location) {
  return lock_validated_internal(mutex, duration, source_location);
}

std::unique_lock<concurrency::shared_counting_mutex> lock_validated(
    concurrency::shared_counting_mutex& mutex, const std::chrono::seconds& duration,
    legacy_embedded_ctl::source_location source_location) {
  return lock_validated_internal(mutex, duration, source_location);
}

std::shared_lock<std::shared_timed_mutex> lock_shared_validated(std::shared_timed_mutex& mutex,
                                                                const std::chrono::seconds& duration,
                                                                legacy_embedded_ctl::source_location source_location) {
  return lock_shared_validated_internal(mutex, duration, source_location);
}

std::shared_lock<concurrency::shared_counting_mutex> lock_shared_validated(
    concurrency::shared_counting_mutex& mutex, const std::chrono::seconds& duration,
    legacy_embedded_ctl::source_location source_location) {
  return lock_shared_validated_internal(mutex, duration, source_location);
}

namespace {

void log_if_time_for_acquiring_a_lock_has_exceeded_threshold(const std::chrono::milliseconds time,
                                                             const std::chrono::seconds threshold,
                                                             legacy_embedded_ctl::source_location source_location) {
  if (time > threshold) {
    log::error("Acquiring lock at {} took longer than [{}] ms.", source_location, time.count());
  }
}

template <typename LOCK>
LOCK lock_with_logging(typename LOCK::mutex_type& mutex, const std::chrono::seconds& threshold,
                       legacy_embedded_ctl::source_location source_location) {
  common::timer timer;
  LOCK lck{mutex};
  timer.stop();
  log_if_time_for_acquiring_a_lock_has_exceeded_threshold(timer.duration(), threshold, source_location);
  return lck;
}

}  // namespace

std::unique_lock<std::shared_mutex> lock_with_logging(std::shared_mutex& mutex, const std::chrono::seconds& threshold,
                                                      legacy_embedded_ctl::source_location source_location) {
  return lock_with_logging<std::unique_lock<std::shared_mutex>>(mutex, threshold, source_location);
}

std::shared_lock<std::shared_mutex> lock_shared_with_logging(std::shared_mutex& mutex,
                                                             const std::chrono::seconds& threshold,
                                                             legacy_embedded_ctl::source_location source_location) {
  return lock_with_logging<std::shared_lock<std::shared_mutex>>(mutex, threshold, source_location);
}

}  // namespace celonis::accelerator::concurrency
