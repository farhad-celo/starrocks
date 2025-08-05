#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

#include "legacy_embedded_ctl/memory/memory_fwd.h"

namespace celonis::accelerator::legacy_embedded_ctl::details {

struct bitset_types final {
  using value_type = std::uint64_t;
  using size_type = std::size_t;
  using bit_index_type = size_type;
  using block_index_type = std::uint32_t;
  using bit_index_in_block_type = std::uint32_t;

  static constexpr std::size_t BLOCK_SIZE() noexcept { return sizeof(value_type) * CHAR_BIT; }

  struct parallelism_setting {
    const bool ENABLE_PARALLELISM{false};
  };

  template <parallelism_setting PARALLELISM_SETTING>
  static constexpr bool parallelism_enabled_v{PARALLELISM_SETTING.ENABLE_PARALLELISM};

  template <parallelism_setting PARALLELISM_SETTING, typename VALUE_TYPE = value_type>
  using block_type =
      std::conditional_t<parallelism_enabled_v<PARALLELISM_SETTING>, std::atomic<VALUE_TYPE>, VALUE_TYPE>;

  template <parallelism_setting PARALLELISM_SETTING>
  using allocator_type = default_tracking_allocator_t<block_type<PARALLELISM_SETTING>>;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
