#pragma once

#include <chrono>
#include <mutex>
#include <shared_mutex>

#include <ctl/source_location.h>

namespace celonis::accelerator::concurrency {

/**
 * Returns the number of concurrent threads supported by the implementation. The value should be considered only a hint.
 * Note: The standard implementation of std::thread::hardware_concurrency could return 0.
 *
 * @return Number of concurrent threads supported. If the value is not well defined or not computable, returns 1.
 */
unsigned int hardware_concurrency() noexcept;

/**
 * @brief tries to acquire the unique ownership for the given mutex within a given time interval
 * @param mutex the mutex to lock (i.e., to acquire ownership for)
 * @param duration the number of seconds for how long to try to acquire the lock before throwing an exception
 * @param source_location the source location from where this function is called (for the potential exception message)
 * @return a valid unique lock on the given mutex which owns the lock
 * @throws internal_exception when the lock could not be acquired within the given duration
 */
[[nodiscard]] std::unique_lock<std::shared_timed_mutex> lock_validated(
    std::shared_timed_mutex& mutex, const std::chrono::seconds& duration,
    ctl::source_location source_location = ctl::source_location{});

/**
 * @brief same as above but for a different mutex type
 */
[[nodiscard]] std::unique_lock<std::timed_mutex> lock_validated(
    std::timed_mutex& mutex, const std::chrono::seconds& duration,
    ctl::source_location source_location = ctl::source_location{});

/**
 * @brief tries to acquire the shared ownership for the given mutex within a given time interval
 * @param mutex the mutex to lock (i.e., to acquire ownership for)
 * @param duration the number of seconds for how long to try to acquire the lock before throwing an exception
 * @param source_location the source location from where this function is called (for the potential exception message)
 * @return a valid shared lock on the given mutex which owns the lock
 * @throws internal_exception when the lock could not be acquired within the given duration
 */
[[nodiscard]] std::shared_lock<std::shared_timed_mutex> lock_shared_validated(
    std::shared_timed_mutex& mutex, const std::chrono::seconds& duration,
    ctl::source_location source_location = ctl::source_location{});

/**
 * @brief acquires unique ownership for the given mutex and produces a log message if it took more than a given time
 * duration
 * @param mutex the mutex to lock (i.e., to acquire ownership for)
 * @param threshold the number of seconds after a log message should be produced
 * @param source_location the source location from where this function is called (for the potential log message)
 * @return a valid unique lock on the given mutex which owns the lock
 */
[[nodiscard]] std::unique_lock<std::shared_mutex> lock_with_logging(
    std::shared_mutex& mutex, const std::chrono::seconds& threshold,
    ctl::source_location source_location = ctl::source_location{});

/**
 * @brief acquires shared ownership for the given mutex and produces a log message if it took more than a given time
 * duration
 * @param mutex the mutex to lock (i.e., to acquire ownership for)
 * @param threshold the number of seconds after a log message should be produced
 * @param source_location the source location from where this function is called (for the potential log message)
 * @return a valid shared lock on the given mutex which owns the lock
 */
[[nodiscard]] std::shared_lock<std::shared_mutex> lock_shared_with_logging(
    std::shared_mutex& mutex, const std::chrono::seconds& threshold,
    ctl::source_location source_location = ctl::source_location{});

}  // namespace celonis::accelerator::concurrency
