#pragma once

#include <type_traits>
#include <utility>

#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

template <typename T>
struct dict_compare {
  inline bool operator()(const std::pair<T, row_id>& l, const std::pair<T, row_id>& r) const
      noexcept(noexcept(std::declval<T>() < std::declval<T>())) {
    return l.first < r.first;
  }
};

}  // namespace celonis::accelerator::memory
