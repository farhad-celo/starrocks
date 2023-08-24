#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>

#ifndef CELOSTAR
#include "modules/cube/execution/tracking/active_operators_registry_fwd.h"
#endif

namespace celonis::accelerator::cube::execution::tracking {

/**
 * Token which stops the execution if its time limit is exceeded. The token must be registered in the
 * active_operators_registry.
 */
class stop_token {
 public:
  /**
   * @param operator_name The operation name (which must outlive the lifetime of the stop token!)
   */
  stop_token(std::string_view operator_name, std::chrono::milliseconds time_limit);

  /**
   * Terminates the execution by throwing an exception if the stop flag is set.
   */
  void stop_execution_if_requested() const;

  /**
   * @return time limit in ms of the stop token
   */
  [[nodiscard]] std::chrono::milliseconds get_time_limit_ms() const;

 private:
  std::string_view operator_name_;
  std::chrono::milliseconds time_limit_ms_;
  std::atomic<bool> stop_flag_{false};

 protected:
#ifndef CELOSTAR
  friend active_operators_registry;
#endif

  /**
   * Sets the stop flag of the stop token.
   */
  void request_stop();
};

}  // namespace celonis::accelerator::cube::execution::tracking
