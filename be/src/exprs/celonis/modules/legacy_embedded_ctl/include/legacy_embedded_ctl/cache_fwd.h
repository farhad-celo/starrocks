#pragma once

#include <functional>
namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * This type can be specialized to implement cache eviction for the legacy_embedded_ctl::cache. The specialization
 * should be callable with a const reference to a VALUE and return true, if the cache entry is no longer in use.
 */
template <typename VALUE>
struct is_cache_entry_unused;

template <typename KEY, typename VALUE, typename HASH = std::hash<KEY>>
class cache;

}  // namespace celonis::accelerator::legacy_embedded_ctl
