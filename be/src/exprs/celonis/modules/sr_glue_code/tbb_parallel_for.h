#pragma once

#include <exception>
#include <glog/logging.h>

#include <tbb/parallel_for.h>

#include <ctl/assert.h>
#include <ctl/mutex.h>

namespace sr_glue_code {

class tbb_error_state {
 public:
  tbb_error_state() = default;

  [[nodiscard]] bool has_error() const { return bool{optional_error_}; }

  void set_error(const std::exception_ptr error) {
    // N.B.: For now we only keep track of the first error message
    if (!optional_error_) {
      optional_error_ = error;
    }
    ++number_of_errors_;
  }

  [[noreturn]] void rethrow_error() const {
    debug_assert(has_error() && number_of_errors_ > 0);
    std::rethrow_exception(optional_error_);
  }

  void rethrow_if_has_error() const {
    if (has_error()) {
      rethrow_error();
    }
  }

  void log_and_rethrow_if_has_error(const std::string_view context_msg) const {
    if (has_error()) {
      LOG(ERROR) << fmt::format("{} (a total of {} errors)", context_msg, number_of_errors());
      rethrow_error();
    }
  }

  [[nodiscard]] int number_of_errors() const {
    debug_assert(has_error() ? number_of_errors_ > 0 : number_of_errors_ == 0);
    return number_of_errors_;
  }

 private:
  std::exception_ptr optional_error_{};
  int number_of_errors_{0};
};

/**
 * @brief We noticed that in SR a thrown exception within a TBB loop can lead to crashes. That is, for some reason it
 * seems like the exception propagation mechanism of TBB (see also the link below) does not work correctly in SR. This
 * proxy wrapper function is a temporary workaround to prevent these crashes. The idea is to catch all exceptions from
 * within a TBB parallel loop and store the error message in some thread-safe helper container (instead of propagating
 * the exception). Note: The execution of other threads continues (a lazy abort is possible but complicates the code
 * slightly). After the TBB parallel loop finished its execution, we check the thread-safe error container helper
 * whether any errors are stored. If so, we return the error as an 'error_state' (see above); otherwise we return
 * std::nullopt. The caller is responsible to check the return value for any errors and handling these accordingly.
 *
 * https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2022-0/exceptions-and-cancellation.html
 */
template <typename TBB_RANGE, typename TBB_LOOP_BODY>
[[nodiscard]] tbb_error_state non_throwing_tbb_parallel_for(const TBB_RANGE& range, const TBB_LOOP_BODY& body) {
  ctl::owning_mutex<tbb_error_state> protected_optional_error_state{};
  tbb::parallel_for(range, [&](const auto& r) {
    try {
      body(r);
    } catch (...) {
      protected_optional_error_state.lock_mutable(
          [&](tbb_error_state& error_state) { error_state.set_error(std::current_exception()); });
    }
  });

  return std::move(protected_optional_error_state).extract_unlocked();
}

/** Same as above but for parallel_for_each */
template <typename TBB_RANGE, typename TBB_LOOP_BODY>
[[nodiscard]] tbb_error_state non_throwing_tbb_parallel_for_each(const TBB_RANGE& range, const TBB_LOOP_BODY& body) {
  ctl::owning_mutex<tbb_error_state> protected_optional_error_state{};
  tbb::parallel_for_each(range, [&](const auto& r) {
    try {
      body(r);
    } catch (...) {
      protected_optional_error_state.lock_mutable(
          [&](tbb_error_state& error_state) { error_state.set_error(std::current_exception()); });
    }
  });

  return std::move(protected_optional_error_state).extract_unlocked();
}

}  // namespace sr_glue_code