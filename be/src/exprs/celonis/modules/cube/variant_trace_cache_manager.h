#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "legacy_embedded_ctl/mutex.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/int_types.h"
#include "modules/common/owned_column_ptr_data.h"
#include "modules/common/trace_types.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/swap_info.h"

namespace celonis::accelerator::cube {

/**
 *  column_pointers may be stored in the variant trace cache for internal i.e. cpp variant computations. In this
 *  case, the trace entries in the cache are not sorted in the order of the VARIANT dictionary.
 */
// TODO(s.griebel) fix naming and member order
class variant_trace_cache_manager {
  struct map_value {
    std::string table_name;
    memory::cache::variant_trace_cache_t variant;
    memory::management::volatile_group_t group;
  };

  const memory::management::swap_info sinfo;
  std::atomic<uint32_t> curr_cache_id;
  legacy_embedded_ctl::owning_mutex<std::unordered_map<std::string, map_value>> locked_variant_trace_caches;

#ifndef CELOSTAR
  std::string get_next_cache_id();

  memory::cache::variant_trace_cache_t retrieve_variant_cache_internal(const std::string& cache_key);

  void store_variant_cache(const std::string& cache_key, const std::string& table_name,
                           memory::cache::variant_trace_cache_t&& cache);
  void store_variant_cache_col_ptrs(const std::string& cache_key, const std::string& table_name,
                                    memory::cache::variant_trace_cache_t&& cache);
#endif

 public:
  explicit variant_trace_cache_manager(const memory::management::swap_info& sinfo);

  [[nodiscard]] size_t size() const;

#ifndef CELOSTAR
  void create_and_store_variant_cache(const std::string& cache_key, const std::string& table_name,
                                      legacy_embedded_ctl::static_array<trace_type> traces,
                                      legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer,
                                      legacy_embedded_ctl::static_array<trace_length_type> trace_lengths);

  void create_and_store_variant_cache_col_ptrs(const std::string& cache_key, const std::string& table_name,
                                               legacy_embedded_ctl::static_array<trace_type> traces,
                                               legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer,
                                               legacy_embedded_ctl::static_array<trace_length_type> trace_lengths,
                                               common::owned_column_ptr_data_t group_id_to_trace_id);

  memory::cache::variant_trace_cache_t retrieve_variant_cache(const std::string& cache_key);
  memory::cache::variant_trace_cache_t retrieve_variant_cache_col_ptrs(const std::string& cache_key);

  size_t erase_tables(const std::unordered_set<std::string>& table_names);
#endif
};

}  // namespace celonis::accelerator::cube
