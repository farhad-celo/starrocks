#pragma once

#include <tbb/spin_mutex.h>

#include "legacy_embedded_ctl/buffer.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::common {

using char_buffer = legacy_embedded_ctl::buffer<char>;
using process_buffer = legacy_embedded_ctl::buffer<int16_t>;

}  // namespace celonis::accelerator::common
