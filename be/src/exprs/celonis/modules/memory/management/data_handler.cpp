#include "data_handler.h"

#include "log/log.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::memory::management {

void drop_short_wait_spans(const common::timer& timer_with_lock, common::execution_context& wait_context) {
  using namespace std::chrono_literals;
  // Reduces the number of recorded wait spans significantly
  if (timer_with_lock.get_currently_passed_time() < 1ms) {
    wait_context.get_span().drop_span();
  }
}

void log_warning_if_swap_in_too_long(common::timer& timer_with_lock, common::timer& timer_after_lock,
                                     const std::string& description,
                                     std::chrono::system_clock::duration max_timer_with_lock_duration) {
  timer_with_lock.stop();
  timer_after_lock.stop();

  if (timer_with_lock.duration() > max_timer_with_lock_duration) {
    log::jwarn("Long SWAP_IN time", {
                                        {"time_with_lock", std::to_string(timer_with_lock.duration().count())},
                                        {"time_without_lock", std::to_string(timer_after_lock.duration().count())},
                                        {"description", description},
                                    });
  }
}

bool data_handler::swap_file_exists(const std::string& swap_file, const swap_info& sinfo) {
#ifdef CELOSTAR
  return false;
#else
  return io::file_exists(swap_file, sinfo);
#endif
}

}  // namespace celonis::accelerator::memory::management