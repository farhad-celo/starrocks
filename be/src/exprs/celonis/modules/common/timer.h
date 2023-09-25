#pragma once

#include <chrono>
#include <ctime>
#include <ostream>
#include <sstream>
#include <string>

#include "modules/common/int_types.h"

namespace celonis::accelerator::common {

class timer {
 public:
  explicit timer() { restart(); }

  virtual ~timer() = default;

  void restart() { start_time_point = std::chrono::steady_clock::now(); }

  void stop() { end_time_point = std::chrono::steady_clock::now(); }

  /**
   * Note: Does not stop the timer!
   */
  [[nodiscard]] std::chrono::milliseconds get_currently_passed_time() const {
    auto current = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(current - start_time_point);
  }

  /**
   * Note: calling this on a running timer (i.e. not timer.stop() -ed) will produce wrong results!
   */
  [[nodiscard]] std::chrono::milliseconds duration() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time_point - start_time_point);
  }

  /**
   * Note: calling this on a running timer (i.e. not timer.stop() -ed) will produce wrong results!
   */
  [[nodiscard]] std::chrono::microseconds duration_us() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(end_time_point - start_time_point);
  }

  [[nodiscard]] auto start() const { return start_time_point; }

  [[nodiscard]] auto end() const { return end_time_point; }

  friend std::ostream& operator<<(std::ostream& os, const timer& timer) {
    return os << R"("timer":{"duration":)" << timer.duration().count() << "}";
  }

 private:
  std::chrono::steady_clock::time_point start_time_point;
  std::chrono::steady_clock::time_point end_time_point;
};

// if clock_gettime(CLOCK_THREAD_CPUTIME_ID, ...) is available
class cputimer {
 public:
  explicit cputimer() { clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_time_point); }

  void stop() { clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_time_point); }

  [[nodiscard]] std::chrono::milliseconds duration() const {
    time_t diffnano = end_time_point.tv_nsec - start_time_point.tv_nsec;
    int64_t diffseconds = end_time_point.tv_sec - start_time_point.tv_sec;
    if (diffnano < 0) {
      diffseconds -= 1;
      diffnano += 1000 * 1000 * 1000;
    }
    return std::chrono::milliseconds{diffseconds * 1000 + diffnano / 1000000};
  }

  friend std::ostream& operator<<(std::ostream& os, const cputimer& timer) {
    return os << R"("timer":{"duration":)" << timer.duration().count() << "}";
  }

 private:
  // TODO(l.karnowski) Migrate to std::timespec when GCC supports it (>= GCC 9). Clang 9 also works.
  timespec start_time_point{};
  timespec end_time_point{0, 0};
};

class wallcputimer {
 public:
  void stop() {
    cputimer_instance.stop();
    timer_instance.stop();
  }

  [[nodiscard]] std::string duration() const {
    std::stringstream time_output;
    time_output << "wall: " << timer_instance.duration().count() << "ms, cpu: " << cputimer_instance.duration().count()
                << "ms";
    return time_output.str();
  }

  [[nodiscard]] std::chrono::milliseconds wallduration() const { return timer_instance.duration(); }

  friend std::ostream& operator<<(std::ostream& os, const wallcputimer& timer) {
    return os << R"("timer": {"wall": )" << timer.timer_instance.duration().count() << R"(, "cpu": )"
              << timer.cputimer_instance.duration().count() << "}";
  }

 private:
  cputimer cputimer_instance;
  timer timer_instance;
};

}  // namespace celonis::accelerator::common
