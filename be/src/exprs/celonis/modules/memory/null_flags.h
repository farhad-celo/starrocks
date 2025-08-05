#pragma once

#include <initializer_list>
#include <memory>

#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/int_types.h"
#include "modules/memory/null_flags_fwd.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

inline memory::null_flags_t create_null_flags() { return std::make_shared<null_flags_bitset_t>(); }
memory::null_flags_t create_null_flags(size_t size, const common::execution_context& context);
inline memory::null_flags_t create_null_flags(const int32_t row_count, const common::execution_context& context) {
  return create_null_flags(static_cast<size_t>(row_count), context);
}
inline memory::null_flags_t create_null_flags(const int64_t row_count, const common::execution_context& context) {
  return create_null_flags(static_cast<size_t>(row_count), context);
}
inline memory::null_flags_t create_null_flags(null_flags_bitset_t&& null_flags) {
  return std::make_shared<null_flags_bitset_t>(std::move(null_flags));
}
inline memory::null_flags_t create_null_flags(const null_flags_bitset_t& null_flags) {
  return std::make_shared<null_flags_bitset_t>(null_flags);
}
[[nodiscard]] memory::null_flags_t create_null_flags(std::initializer_list<bool> values,
                                                     const common::execution_context& context);

}  // namespace celonis::accelerator::memory
