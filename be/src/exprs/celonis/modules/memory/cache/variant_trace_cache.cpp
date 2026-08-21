#include "variant_trace_cache.h"

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "ctl/assert.h"
#include "modules/common/aligned_blocked_range.h"
#include "modules/memory/column_pointers.h"

namespace celonis::accelerator::memory::cache {

variant_trace_cache::variant_trace_cache(std::shared_ptr<management::pointer_data_handler<trace_type>> data_handler,
                                         management::raw_data_handler_t<trace_length_type> trace_lengths,
                                         const std::string& cache_key,
                                         memory::column_ptrs_t group_id_to_variant_id_mapping_col_ptrs)
    : variant_trace_cache{std::move(data_handler), std::move(trace_lengths), cache_key} {
  // assignment due to delegating constructor
  optional_group_id_to_trace_id_ = std::move(group_id_to_variant_id_mapping_col_ptrs);
}

variant_trace_cache::variant_view_t variant_trace_cache::variant_view_for_id(
    const variant_id_t variant_id, const common::execution_context& ctx) const {
  common::runtime_assert(variant_id.get() < get_num_traces(),
                         "The given variant ID [{}] is invalid. Max valid ID is [{}].", variant_id.get(),
                         get_num_traces());
  return {get_traces(ctx)[variant_id], get_trace_lengths(ctx)[variant_id]};
}

bool variant_trace_cache::has_group_id_to_variant_id_mapping() const {
  return optional_group_id_to_trace_id_.has_value();
}

row_id variant_trace_cache::get_group_id_domain() const {
  debug_assert(has_group_id_to_variant_id_mapping());
  return static_cast<row_id>(optional_group_id_to_trace_id_.value()->get_row_count());
}

const memory::column_ptrs_t& variant_trace_cache::group_id_to_variant_id_mapping_col_ptrs() const {
  debug_assert(has_group_id_to_variant_id_mapping());
  return optional_group_id_to_trace_id_.value();
}

namespace {

enum mapping_to_compute { GROUP_ID_TO_VARIANT_ID, VARIANT_ID_TO_GROUP_SIZE };

template <mapping_to_compute MAPPING_TO_COMPUTE, typename MAPPING_T>
void compute_and_fill_mapping_from_col_ptrs(const variant_trace_cache& variant_entries, MAPPING_T& result_mapping) {
  common::runtime_assert(
      variant_entries.has_group_id_to_variant_id_mapping(),
      "The variants must have been computed internally: The group ID to variant ID mapping must exist.");

  const auto group_id_domain{variant_entries.get_group_id_domain()};

  memory::cast_execute_column_pointers(
      [group_id_domain, &result_mapping]<typename TUPLE>(const TUPLE& tpl) {
        const auto variant_id_accessor{std::get<0>(tpl).get_const_accessor()};
        common::safe_aligned_blocked_range<row_id> range{0, group_id_domain, 1 << 17};

        using local_mapping_t = std::vector<group_id_to_variant_id_mapping_t::value_type>;
        tbb::enumerable_thread_specific<local_mapping_t> thread_local_result_mappings{};

        tbb::parallel_for(range, [&variant_id_accessor = std::as_const(variant_id_accessor),
                                  &thread_local_result_mappings](const auto& range) {
          auto& local_result_mapping{thread_local_result_mappings.local()};
          for (row_id group_id{range.begin}; group_id < range.end; ++group_id) {
            const auto unchecked_variant_id{variant_id_accessor[group_id]};
            if (unchecked_variant_id != variant_trace_cache::INVALID_VARIANT_ID.get()) {
              local_result_mapping.emplace_back(variant_trace_cache::group_id_t{group_id},
                                                variant_trace_cache::variant_id_t{unchecked_variant_id});
            }
          }
        });

        std::ranges::for_each(thread_local_result_mappings, [&result_mapping](const local_mapping_t& local_mapping) {
          std::ranges::for_each(local_mapping, [&result_mapping](const auto& local_mapping_value) {
            if constexpr (MAPPING_TO_COMPUTE == GROUP_ID_TO_VARIANT_ID) {
              result_mapping.insert(local_mapping_value);
            } else if constexpr (MAPPING_TO_COMPUTE == VARIANT_ID_TO_GROUP_SIZE) {
              const auto variant_id{local_mapping_value.second};
              ++result_mapping.at(variant_id);
            } else {
              static_assert(ctl::always_false_v<decltype(MAPPING_TO_COMPUTE)>);
            }
          });
        });
      },
      *variant_entries.group_id_to_variant_id_mapping_col_ptrs());
}

}  // anonymous namespace

group_id_to_variant_id_mapping_t compute_group_id_to_variant_id_mapping_from_col_ptrs(
    const variant_trace_cache& variant_entries) {
  group_id_to_variant_id_mapping_t group_id_to_variant_id_mapping{};
  compute_and_fill_mapping_from_col_ptrs<GROUP_ID_TO_VARIANT_ID>(variant_entries, group_id_to_variant_id_mapping);
  return group_id_to_variant_id_mapping;
}

variant_id_to_group_size_mapping_t compute_variant_id_to_group_size_mapping_from_col_ptrs(
    const variant_trace_cache& variant_entries, const common::execution_context& ctx) {
  const auto number_of_variants{variant_entries.get_num_traces()};
  const auto& sub_ctx{ctx.create_sub_context("compute_variant_id_to_group_size_mapping_from_col_ptrs", {})};
  variant_id_to_group_size_mapping_t variant_id_to_group_size_mapping{
      ctl::make_static_array_value_init<size_t>(number_of_variants, ALLOC_MSG(ctl::RETURN_VALUE_MSG))};
  compute_and_fill_mapping_from_col_ptrs<VARIANT_ID_TO_GROUP_SIZE>(variant_entries, variant_id_to_group_size_mapping);
  return variant_id_to_group_size_mapping;
}

void variant_trace_cache::set_delete_from_disk_when_destructed() {
  data_handle->set_delete_from_disk_when_destructed(true);
  trace_lengths->set_delete_from_disk_when_destructed(true);
  if (optional_group_id_to_trace_id_.has_value()) {
    optional_group_id_to_trace_id_.value()->set_delete_from_disk_when_destructed(true);
  }
}

}  // namespace celonis::accelerator::memory::cache
