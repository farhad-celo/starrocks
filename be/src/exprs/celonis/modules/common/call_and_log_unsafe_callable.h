#pragma once

#include <exception>
#include <string>

#include "log/log.h"

namespace celonis::accelerator::common {

template <typename CALLABLE>
void call_and_log_unsafe_callable(CALLABLE&& callable, const std::string& msg) noexcept {
  try {
    callable();
  } catch (const std::exception& ex) {
    log::error("{} due to '{}'.", msg, ex.what());
  } catch (...) {
    log::error("{}.", msg);
  }
}

}  // namespace celonis::accelerator::common