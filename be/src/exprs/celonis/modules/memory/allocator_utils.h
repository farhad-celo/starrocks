#pragma once

#include <array>
#include <optional>

#include "modules/common/int_types.h"

namespace celonis::accelerator::memory {

struct allocator_stats {
  size_t allocated;
  size_t active;
  size_t metadata_size;
  size_t resident;
  size_t mapped;
  size_t retained;
};

std::optional<allocator_stats> get_allocator_stats();

/**
 * Initializes a mib, which speeds up calls to the purge function in jemalloc
 */
std::pair<std::array<size_t, 3>, size_t> init_purge_mib();

/**
 * Releases all unused buffers held by the used malloc implementation
 */
void release_unused_buffers();

}  // namespace celonis::accelerator::memory
