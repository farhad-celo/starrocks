#pragma once

#include <functional>
#include <future>
#include <string>
#include <unordered_map>

#include "modules/memory/cache/column_register_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/warnings.h"

namespace celonis::accelerator::memory::cache {

struct cache_entry;

using cache_entry_t = std::shared_future<cache_entry>;
using cache_entry_function_t = std::function<cache_entry(const column_register&)>;
using multi_cache_entries_function_t =
    std::function<std::unordered_map<std::string, cache_entry>(const cache_column_register_map_t&)>;
using committed_function_t = std::function<void(const std::set<std::string>& cache_keys)>;
using rollback_function_t = std::function<void(const std::set<std::string>& calculated_cache_keys)>;

}  // namespace celonis::accelerator::memory::cache
