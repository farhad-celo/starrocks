#pragma once

#include <mutex>
#include <optional>
#include <string>

#include <tbb/parallel_for.h>

#include <ctl/assert.h>

namespace sr_glue_code {

namespace details {

class tbb_error_propagation_helper {
 public:
  tbb_error_propagation_helper() = default;

  [[nodiscard]] bool has_error() const {
    std::shared_lock lock{mtx_};
    return optional_error_msg_.has_value();
  }

  void set_error(const std::string_view error_msg) {
    std::unique_lock lock{mtx_};
    // N.B.: For now we only keep track of the first error message
    if (!optional_error_msg_.has_value()) {
      optional_error_msg_ = error_msg;
    }
    ++number_of_errors_;
  }

  [[nodiscard]] const std::string& error_message() const {
    std::shared_lock lock{mtx_};
    debug_assert(has_error());
    return optional_error_msg_.value();
  }

  [[nodiscard]] int number_of_errors() const {
    std::shared_lock lock{mtx_};
    return number_of_errors_;
  }

 private:
  mutable std::shared_mutex mtx_{};
  std::optional<std::string> optional_error_msg_{std::nullopt};
  int number_of_errors_{0};
};

}  // namespace details

struct error_state {
  std::string error_msg{};
  int number_of_errors{0};
};

using optional_error_state_t = std::optional<error_state>;

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
[[nodiscard]] optional_error_state_t non_throwing_tbb_parallel_for(const TBB_RANGE& range, const TBB_LOOP_BODY& body) {
  details::tbb_error_propagation_helper error_propagation_helper{};
  tbb::parallel_for(range, [&](const auto& r) {
    try {
      body(r);
    } catch (const std::exception& ex) {
      error_propagation_helper.set_error(ex.what());
    } catch (...) {
      error_propagation_helper.set_error("Unknown error");
    }
  });

  if (error_propagation_helper.has_error()) [[unlikely]] {
    return std::make_optional<error_state>(error_propagation_helper.error_message(),
                                           error_propagation_helper.number_of_errors());
  }

  return std::nullopt;
}

/** Same as above but for parallel_for_each */
template <typename TBB_RANGE, typename TBB_LOOP_BODY>
[[nodiscard]] optional_error_state_t non_throwing_tbb_parallel_for_each(const TBB_RANGE& range,
                                                                        const TBB_LOOP_BODY& body) {
  details::tbb_error_propagation_helper error_propagation_helper{};
  tbb::parallel_for_each(range, [&](const auto& r) {
    try {
      body(r);
    } catch (const std::exception& ex) {
      error_propagation_helper.set_error(ex.what());
    } catch (...) {
      error_propagation_helper.set_error("Unknown error");
    }
  });

  if (error_propagation_helper.has_error()) [[unlikely]] {
    return std::make_optional<error_state>(error_propagation_helper.error_message(),
                                           error_propagation_helper.number_of_errors());
  }

  return std::nullopt;
}

}  // namespace sr_glue_code