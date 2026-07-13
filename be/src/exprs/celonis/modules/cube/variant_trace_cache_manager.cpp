#include "variant_trace_cache_manager.h"

#include <chrono>
#include <functional>

#include <fmt/format.h>

#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utility.h"
#include "log/log.h"
#include "modules/cube/variant_trace_utils.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/management/pointer_data_handler.h"

namespace celonis::accelerator::cube {

#ifndef CELOSTAR
namespace {

inline std::string decorate_cache_key_col_ptrs(const std::string& variant_trace_cache_key) {
  return fmt::format("${}_WITH_COL_PTRS$", variant_trace_cache_key);
}

}  // namespace
#endif

variant_trace_cache_manager::variant_trace_cache_manager(const memory::management::swap_info& sinfo)
    : sinfo(sinfo.swap_into_sub_dir("variants", memory::management::persistence_status::NON_PERSISTENT)),
      curr_cache_id(0) {}

size_t variant_trace_cache_manager::size() const {
  return locked_variant_trace_caches.lock_shared(
      [](const auto& variant_trace_caches) { return variant_trace_caches.size(); });
}

#ifndef CELOSTAR
void variant_trace_cache_manager::create_and_store_variant_cache(
    const std::string& cache_key, const std::string& table_name, legacy_embedded_ctl::static_array<trace_type> traces,
    legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer,
    legacy_embedded_ctl::static_array<trace_length_type> trace_lengths) {
  const details::cached_variants::caching_meta_data caching_meta_data{
      .cache_key = cache_key, .cache_id = get_next_cache_id(), .swap_info = sinfo};

  auto [data_handler, trace_lengths_data_handler]{details::cached_variants::create_trace_handlers_internal(
      std::move(traces), std::move(trace_buffer), std::move(trace_lengths), caching_meta_data)};

  auto cache_entry{std::make_shared<memory::cache::variant_trace_cache>(
      std::move(data_handler), std::move(trace_lengths_data_handler), cache_key)};

  store_variant_cache(cache_key, table_name, std::move(cache_entry));
}

void variant_trace_cache_manager::create_and_store_variant_cache_col_ptrs(
    const std::string& cache_key, const std::string& table_name, legacy_embedded_ctl::static_array<trace_type> traces,
    legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer,
    legacy_embedded_ctl::static_array<trace_length_type> trace_lengths,
    common::owned_column_ptr_data_t group_id_to_trace_id) {
  const details::cached_variants::caching_meta_data caching_meta_data{
      .cache_key = decorate_cache_key_col_ptrs(cache_key), .cache_id = get_next_cache_id(), .swap_info = sinfo};

  auto handlers{details::cached_variants::create_trace_handlers_internal(std::move(traces), std::move(trace_buffer),
                                                                         std::move(trace_lengths), caching_meta_data)};

  auto group_id_to_trace_id_mapping_col_ptrs{details::cached_variants::create_column_ptrs_from_owned_data_internal(
      std::move(group_id_to_trace_id), caching_meta_data)};

  // create column_ptrs_t from raw_column_ptrs and provide a data handler
  auto cache_entry{std::make_shared<memory::cache::variant_trace_cache>(
      std::move(handlers.trace_data_handler), std::move(handlers.trace_lengths_data_handler), cache_key,
      std::move(group_id_to_trace_id_mapping_col_ptrs))};

  store_variant_cache_col_ptrs(cache_key, table_name, std::move(cache_entry));
}

void variant_trace_cache_manager::store_variant_cache(const std::string& cache_key, const std::string& table_name,
                                                      memory::cache::variant_trace_cache_t&& cache) {
  using namespace std::chrono_literals;
  locked_variant_trace_caches.try_lock_mutable_for(
      60s, [this, &cache_key, &table_name, &cache](auto& variant_trace_caches) {
        if (variant_trace_caches.find(cache_key) != variant_trace_caches.end()) {
          log::jwarn("variant entry with the given cache key does already exist in the cache manager.",
                     {{"cache_key", cache_key}});
          return;
        }

        // TODO(s.griebel) This is a problematic construction but not worse than before
        std::function<bool()> clean_up_callback = [this, cache_key]() -> bool {
          locked_variant_trace_caches.try_lock_mutable_for(
              60s, [cache_key](auto& variant_trace_caches) { variant_trace_caches.erase(cache_key); });
          return true;
        };

        memory::management::volatile_group_t managed_group =
            std::make_shared<memory::management::volatile_managed_memory_group>(
                fmt::format("Trace Cache: {}", cache_key), clean_up_callback);
        managed_group->add_to_group(cache->data_handle);
        managed_group->add_to_group(cache->trace_lengths);
        if (cache->has_group_id_to_variant_id_mapping()) {
          managed_group->add_to_group(cache->group_id_to_variant_id_mapping_col_ptrs()->get_abstract());
        }

        sinfo.memory_manager()->register_volatile_group(managed_group);
        // As we are using shared ptrs, no locking should be required here even in the case of a race condition.
        variant_trace_caches[cache_key] = map_value{.table_name = table_name, .variant = cache, .group = managed_group};
      });
}

void variant_trace_cache_manager::store_variant_cache_col_ptrs(const std::string& cache_key,
                                                               const std::string& table_name,
                                                               memory::cache::variant_trace_cache_t&& cache) {
  store_variant_cache(decorate_cache_key_col_ptrs(cache_key), table_name, std::move(cache));
}

memory::cache::variant_trace_cache_t variant_trace_cache_manager::retrieve_variant_cache_internal(
    const std::string& cache_key) {
  return locked_variant_trace_caches.lock_shared([&cache_key](const auto& variant_trace_caches) {
    auto it = variant_trace_caches.find(cache_key);
    if (it == variant_trace_caches.end()) {
      return memory::cache::variant_trace_cache_t(nullptr);
    }
    return it->second.variant;
  });
}

memory::cache::variant_trace_cache_t variant_trace_cache_manager::retrieve_variant_cache(const std::string& cache_key) {
  return retrieve_variant_cache_internal(cache_key);
}

memory::cache::variant_trace_cache_t variant_trace_cache_manager::retrieve_variant_cache_col_ptrs(
    const std::string& cache_key) {
  return retrieve_variant_cache_internal(decorate_cache_key_col_ptrs(cache_key));
}

std::string variant_trace_cache_manager::get_next_cache_id() { return std::to_string(curr_cache_id++); }

size_t variant_trace_cache_manager::erase_tables(const std::unordered_set<std::string>& table_names) {
  return locked_variant_trace_caches.lock_mutable([&table_names](auto& variant_trace_caches) {
    return std::erase_if(variant_trace_caches, [&table_names](const auto& key_value_pair) {
      if (table_names.contains(key_value_pair.second.table_name)) {
        key_value_pair.second.variant->set_delete_from_disk_when_destructed();
        return true;
      }

      return false;
    });
  });
}
#endif

}  // namespace celonis::accelerator::cube
