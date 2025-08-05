#include "legacy_embedded_ctl/utils/type_utils.h"

#include <boost/core/demangle.hpp>

namespace celonis::accelerator::legacy_embedded_ctl::utils::details {

std::string demangle_type_name(const std::type_info& type_info) { return boost::core::demangle(type_info.name()); }

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils::details
