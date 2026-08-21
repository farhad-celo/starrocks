#include "null_flags.h"

#include <ctl/bitset.h>

namespace celonis::accelerator::memory {

memory::null_flags_t create_null_flags(const size_t size) {
  return std::make_shared<ctl::dynamic_bitset_t>(size, false);
}

}  // namespace celonis::accelerator::memory
