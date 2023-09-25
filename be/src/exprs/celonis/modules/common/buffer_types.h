#pragma once

#include <tbb/spin_mutex.h>

#include "ctl/buffer.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::common {

using char_buffer = ctl::buffer<char>;
using process_buffer = ctl::buffer<int16_t>;

}  // namespace celonis::accelerator::common
