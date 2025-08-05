#pragma once

#include <string>
#include <typeinfo>

namespace celonis::accelerator::legacy_embedded_ctl::utils {

namespace details {

[[nodiscard]] std::string demangle_type_name(const std::type_info& type_info);

}  // namespace details

template <typename T>
[[nodiscard]] std::string type_name() {
  return details::demangle_type_name(typeid(T));
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils
