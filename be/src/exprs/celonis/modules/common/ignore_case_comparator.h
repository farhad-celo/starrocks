#pragma once

#include <string>

#include "modules/common/int_types.h"

namespace celonis::accelerator::common {

struct ignore_case_comparator_less final {
  bool operator()(const std::string& lhs, const std::string& rhs) const;
};

struct ignore_case_comparator_equal final {
  bool operator()(const std::string& lhs, const std::string& rhs) const;
};

struct ignore_case_hasher final {
  size_t operator()(std::string key) const;
};

template <typename STRONG_STRING>
struct strong_string_ignore_case_comparator_less final {
  bool operator()(const STRONG_STRING& lhs, const STRONG_STRING& rhs) const {
    return ignore_case_comparator_less{}(lhs.get(), rhs.get());
  };
};

template <typename STRONG_STRING>
struct strong_string_ignore_case_comparator_equal final {
  bool operator()(const STRONG_STRING& lhs, const STRONG_STRING& rhs) const {
    return ignore_case_comparator_equal{}(lhs.get(), rhs.get());
  };
};

template <typename STRONG_STRING>
struct strong_string_ignore_case_hasher final {
  size_t operator()(STRONG_STRING key) const { return ignore_case_hasher{}(key.get()); };
};

}  // namespace celonis::accelerator::common
