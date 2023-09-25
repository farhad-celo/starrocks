#pragma once

#include <atomic>
#include <chrono>
#include <shared_mutex>

namespace celonis::accelerator::concurrency {

/** Wrapper for std::shared_timed_mutex with info on waiting threads.
 *
 *  This std::shared_timed_mutex wrapper provides a method to query whether another thread is waiting on this mutex when
 *  this mutex is exclusively locked.
 */
class shared_counting_mutex {
 public:
  void lock_shared() {
    ++count;
    mutex.lock_shared();
  }

  template <class REP, class PERIOD>
  bool try_lock_shared_for(const std::chrono::duration<REP, PERIOD>& relative_time) {
    ++count;
    auto success = mutex.try_lock_shared_for(relative_time);
    if (!success) {
      --count;
    }
    return success;
  }

  void unlock_shared() {
    --count;
    mutex.unlock_shared();
  }

  void lock() {
    ++count;
    mutex.lock();
  }

  template <class REP, class PERIOD>
  bool try_lock_for(const std::chrono::duration<REP, PERIOD>& relative_time) {
    ++count;
    auto success = mutex.try_lock_for(relative_time);
    if (!success) {
      --count;
    }
    return success;
  }

  void unlock() {
    --count;
    mutex.unlock();
  }

  /** Returns whether other threads are waiting for this shared_counting_mutex
   *
   * The result is only valid if the mutex is currently exclusively locked
   */
  [[nodiscard]] bool other_threads_waiting() const { return count > 1; }

  bool has_lock() { return count > 0; }

 private:
  std::atomic<size_t> count{0};
  std::shared_timed_mutex mutex;
};

}  // namespace celonis::accelerator::concurrency
