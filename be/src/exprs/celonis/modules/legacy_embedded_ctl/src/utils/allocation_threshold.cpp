#include "legacy_embedded_ctl/utils/allocation_threshold.h"

#include <mutex>

#include "legacy_embedded_ctl/exception.h"

namespace celonis::accelerator::legacy_embedded_ctl::utils {

allocation_threshold::allocation_threshold(const double value) : value_{value} {
  if (!(0 <= value && value < 1)) {
    throw failed_assertion{"Invalid memory allocation threshold [{}]. Value must be in the interval [0, 1).", value};
  }
}

allocation_threshold& allocation_threshold::operator=(const double value) {
  return *this = allocation_threshold{value};
}

double allocation_threshold::get() const noexcept { return value_; }

double allocation_threshold::as_percentage() const noexcept { return get() * 100; }

allocation_threshold::operator double() const noexcept { return get(); }

allocation_threshold allocation_priority_to_allocation_threshold(const utils::allocation_priority priority) {
  /* Represents the percentage of free memory we want to keep after allocations (for the respective priorities). */
  const auto& allocation_threshold_config{allocation_threshold_config::instance()};
  switch (priority) {
    case utils::allocation_priority::LOW:
      return allocation_threshold_config.low_priority_threshold();
    case utils::allocation_priority::MEDIUM:
      return allocation_threshold_config.medium_priority_threshold();
    case utils::allocation_priority::HIGH:
      return allocation_threshold_config.high_priority_threshold();
    default:
      return allocation_threshold_config.default_threshold();
  }
}

void allocation_threshold_config::initialize(const double default_memory_allocation_threshold) {
  static std::once_flag flag{};
  std::call_once(flag, [&]() {
    const allocation_threshold config_threshold{default_memory_allocation_threshold};  // strong typing
    initialized_instance_.reset(new allocation_threshold_config{config_threshold});
  });
}

const allocation_threshold_config& allocation_threshold_config::instance() {
  if (initialized_instance_ != nullptr) {
    return *initialized_instance_;
  }
  static const allocation_threshold_config default_instance{allocation_threshold{0.1}};
  return default_instance;
}

allocation_threshold allocation_threshold_config::default_threshold() const noexcept {
  return default_memory_threshold_;
}

allocation_threshold allocation_threshold_config::low_priority_threshold() const noexcept {
  return default_threshold();
}

allocation_threshold allocation_threshold_config::medium_priority_threshold() const {
  return allocation_threshold{low_priority_threshold() * 0.8};
}

allocation_threshold allocation_threshold_config::high_priority_threshold() const {
  return allocation_threshold{low_priority_threshold() * 0.6};
}

allocation_threshold_config::allocation_threshold_config(const allocation_threshold default_threshold) noexcept
    : default_memory_threshold_{default_threshold} {}

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils
