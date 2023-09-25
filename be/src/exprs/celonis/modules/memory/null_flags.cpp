#include "null_flags.h"

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/tracking/dynamic_bitset_with_context_tracking.h"

namespace celonis::accelerator::memory {

memory::null_flags_t create_null_flags(const std::initializer_list<bool> values,
                                       const common::execution_context& context) {
  auto result{create_null_flags(values.size(), context)};
  for (null_flags_bitset_t::size_type i{0}; i < result->size(); ++i) {
    result->set(i, std::data(values)[i]);
  }
  return result;
}

memory::null_flags_t create_null_flags(const size_t size, const common::execution_context& context) {
  return memory::tracking::make_tracked_shared_dynamic_bitset_t(size, context);
}

}  // namespace celonis::accelerator::memory
