#pragma once

#include <type_traits>

#include <ctl/array_view.h>

#include "modules/common/int_types.h"

namespace celonis::accelerator {

using trace_element_type = int16_t;
using trace_type = const trace_element_type*;
using trace_buffer_type = std::remove_const_t<std::remove_pointer_t<trace_type>>;
using trace_length_type = size_t;

using trace_view_t = ctl::array_view<const trace_element_type>;
using mut_trace_view_t = ctl::array_view<trace_element_type>;

}  // namespace celonis::accelerator
