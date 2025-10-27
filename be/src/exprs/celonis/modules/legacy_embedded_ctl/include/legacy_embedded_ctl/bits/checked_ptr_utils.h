#pragma once

#include <type_traits>

#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/utils/type_utils.h"

namespace celonis::accelerator::legacy_embedded_ctl::details {

void check_not_null(const auto& ptr) {
  if (!(ptr)) {  // operator bool()
    throw[]() {
      // Inside an IIFE lambda since clang-tidy complains otherwise
      null_pointer_exception ex{};
      ex.add_or_overwrite("type_name", utils::type_name<typename std::remove_reference_t<decltype(ptr)>::value_type>());
      return ex;
    }
    ();
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
