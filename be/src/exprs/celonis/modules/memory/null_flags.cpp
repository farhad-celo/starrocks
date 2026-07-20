#include "null_flags.h"

#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "modules/common/execution_context.h"
#include "modules/memory/tracking/spawn_allocator_from_context.h"

namespace celonis::accelerator::memory {

namespace {

[[nodiscard]] std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_t> make_tracked_shared_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context) {
  return std::make_shared<legacy_embedded_ctl::dynamic_bitset_t>(
      size, false,
      tracking::spawn_allocator<legacy_embedded_ctl::dynamic_bitset_t::block_type>(
          context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG),
          legacy_embedded_ctl::utils::allocation_priority::LOW, false));
}

}  // namespace

memory::null_flags_t create_null_flags(const size_t size) {
  return make_tracked_shared_dynamic_bitset_t(size, common::execution_context{});
}

}  // namespace celonis::accelerator::memory
