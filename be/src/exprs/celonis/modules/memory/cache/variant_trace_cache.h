#pragma once

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/named_type.h"
#include "modules/common/exceptions.h"
#include "modules/common/shared_types.h"
#include "modules/common/trace_types.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::cube {
class variant_trace_cache_manager;
}  // namespace celonis::accelerator::cube

namespace celonis::accelerator::memory::cache {
/**
 * Variant trace cache entries store the unique variants computed for a (activity) column.
 * This optionally stores a column pointers for the variant column, which can be used to serve
 * 'internal' i.e. non-operator variant computations, given an activity column.
 */
class variant_trace_cache {
  friend class cube::variant_trace_cache_manager;

 public:
  using group_id_t = legacy_embedded_ctl::named_type<row_id, struct group_id_tag, legacy_embedded_ctl::comparable, legacy_embedded_ctl::hashable,
                                     legacy_embedded_ctl::implicitly_convertible_to<row_id>::templ, legacy_embedded_ctl::printable>;
  using variant_id_t = legacy_embedded_ctl::named_type<row_id, struct variant_id_tag, legacy_embedded_ctl::comparable, legacy_embedded_ctl::hashable,
                                       legacy_embedded_ctl::implicitly_convertible_to<row_id>::templ, legacy_embedded_ctl::printable>;
  using variant_view_t = legacy_embedded_ctl::array_view<const trace_element_type>;
  static constexpr variant_id_t INVALID_VARIANT_ID{0};  // ID/Index of the empty/null variant

  variant_trace_cache(std::shared_ptr<management::pointer_data_handler<trace_type>> data_handler,
                      management::raw_data_handler_t<trace_length_type> trace_lengths, const std::string& cache_key)
      : data_handle{std::move(data_handler)}, trace_lengths{std::move(trace_lengths)} {
    const size_t num_traces{data_handle->get_size()};
    if (num_traces > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
      throw common::cpm_exception{
          "Size of variant trace cache [{}] exceeds the limit of [{}]. Size of variant trace cache is [{}].", cache_key,
          std::numeric_limits<row_id>::max(), num_traces};
    }
  }

  variant_trace_cache(std::shared_ptr<management::pointer_data_handler<trace_type>> data_handler,
                      management::raw_data_handler_t<trace_length_type> trace_lengths, const std::string& cache_key,
                      memory::column_ptrs_t group_id_to_variant_id_mapping_col_ptrs);

  using traces_accessor_t = management::pointer_data_handler<trace_type>::const_data_accessor_t;

  [[nodiscard]] traces_accessor_t get_traces(const common::execution_context& context) const {
    return data_handle->get_const_data(context);
  }

  using trace_lengths_accessor_t = management::raw_data_handler<trace_length_type>::const_data_accessor_t;

  [[nodiscard]] trace_lengths_accessor_t get_trace_lengths(const common::execution_context& context) const {
    return trace_lengths->get_const_data(context);
  }

  [[nodiscard]] variant_view_t variant_view_for_id(variant_id_t variant_id, const common::execution_context& ctx) const;

  [[nodiscard]] row_id get_num_traces() const { return static_cast<row_id>(data_handle->get_size()); }

  [[nodiscard]] bool has_group_id_to_variant_id_mapping() const;

  [[nodiscard]] row_id get_group_id_domain() const;

  [[nodiscard]] const memory::column_ptrs_t& group_id_to_variant_id_mapping_col_ptrs() const;

  // Deprecated
  [[nodiscard]] std::optional<memory::column_ptrs_t> get_case_to_trace_col_ptrs() const {
    return optional_group_id_to_trace_id_;
  }
  // Deprecated
  [[nodiscard]] row_id get_num_cases() const { return get_group_id_domain(); }

  void set_delete_from_disk_when_destructed();

 private:
  std::shared_ptr<management::pointer_data_handler<trace_type>> data_handle;
  management::raw_data_handler_t<trace_length_type> trace_lengths;
  /*
   * The group IDs don't have any semantics (e.g., they don't necessarily refer to a case table row), they also don't
   * need to be densely numbered anymore. Thus, one can not (always) simply use the range [0, N) as group IDs anymore
   * and expect a valid variant ID to exist for each value in such a range. Instead, we have a mapping with the size of
   * the largest group ID + 1 (i.e., the group domain) which maps from group ID to its variant ID if such a mapping
   * (i.e., group ID) exists. Otherwise, the 'INVALID_VARIANT_ID' value is returned.
   * Note: When the group IDs are rows in the case table or otherwise densely numbered, one can safely assume to get a
   * valid variant ID (mapping value) for each group ID.
   */
  std::optional<memory::column_ptrs_t> optional_group_id_to_trace_id_;  // only to indicate nullability
};

using group_id_to_variant_id_mapping_t =
    std::unordered_map<variant_trace_cache::group_id_t, variant_trace_cache::variant_id_t>;
using variant_id_to_group_size_mapping_t = legacy_embedded_ctl::static_array<size_t>;

/** Utilities (mostly for testing) which return various mappings from/to valid group/variant IDs */
[[nodiscard]] group_id_to_variant_id_mapping_t compute_group_id_to_variant_id_mapping_from_col_ptrs(
    const variant_trace_cache& variant_entries);

[[nodiscard]] variant_id_to_group_size_mapping_t compute_variant_id_to_group_size_mapping_from_col_ptrs(
    const variant_trace_cache& variant_entries, const common::execution_context& ctx);

}  // namespace celonis::accelerator::memory::cache
