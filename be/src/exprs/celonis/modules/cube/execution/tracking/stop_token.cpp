#include "stop_token.h"

#ifndef CELOSTAR
#include "modules/common/date/date_time_constants.h"
#endif
#include "modules/common/exceptions.h"

namespace celonis::accelerator::cube::execution::tracking {

stop_token::stop_token(const std::string_view operator_name, const std::chrono::milliseconds time_limit_ms)
    : operator_name_{operator_name}, time_limit_ms_{time_limit_ms} {}

void stop_token::stop_execution_if_requested() const {
#ifndef CELOSTAR
  if (stop_flag_.load(std::memory_order_relaxed)) {
    throw common::cpm_exception{"{}: Exceeded execution time limit of {} minutes.", operator_name_,
                                time_limit_ms_.count() / date::MILLIS_PER_MINUTE};
  }
#endif
}

std::chrono::milliseconds stop_token::get_time_limit_ms() const { return time_limit_ms_; }

void stop_token::request_stop() { stop_flag_.store(true); }

}  // namespace celonis::accelerator::cube::execution::tracking