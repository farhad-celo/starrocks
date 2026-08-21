#include "join_projection_factories.h"

#include <atomic>

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <ctl/assert.h>
#include <ctl/concepts.h>
#include <ctl/conversion.h>
#include <ctl/type_traits.h>

#include "modules/common/aligned_blocked_range.h"
#include "modules/common/case_aligned_range.h"
#include "modules/common/int_types.h"
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"

namespace celonis::accelerator::memory {

namespace {

template <ctl::standard_integer T>
class safe_max_int final {
 public:
  safe_max_int() noexcept = default;

  [[nodiscard]] T get() const noexcept { return max.load(std::memory_order_relaxed); }

  void update(const T local_max) {
    auto current_max{get()};
    while (current_max < local_max && !max.compare_exchange_weak(current_max, local_max, std::memory_order_relaxed)) {
      // do nothing
    }
  }

 private:
  std::atomic<T> max{-1};
};

/** Finds the largest group ID in the mapping and returns it + 1 as domain value */
[[nodiscard]] row_id compute_group_id_domain(const value_idx_to_group_id_mapping_t& value_idx_to_group_id_mapping) {
  return memory::cast_execute_projection_vector(
      [](const auto& mapping) {
        if (mapping.empty()) {
          return row_id{0};
        }

        // Unfortunately, there are cases where the mapping is not sorted. Thus, we can not simply take the last element
        safe_max_int<row_id> max_group_id{};
        tbb::parallel_for(common::safe_aligned_blocked_range{0ul, mapping.size()},
                          [&mapping = std::as_const(mapping), &max_group_id](const auto& range) {
                            row_id local_max_group_id{-1};
                            for (size_t idx{range.begin}; idx < range.end; ++idx) {
                              local_max_group_id = std::max<row_id>(local_max_group_id, mapping[idx]);
                            }
                            max_group_id.update(local_max_group_id);
                          });
        return max_group_id.get() + 1;
      },
      value_idx_to_group_id_mapping);
}

}  // anonymous namespace

row_id group_id_mapping_and_group_id_domain::get_or_compute_group_id_domain() {
  if (!optional_group_id_domain.has_value()) {
    optional_group_id_domain = compute_group_id_domain(value);
  }
  // TODO(n.weber): TBD - Should this be validated in debug builds?
  //  debug_assert(*optional_group_id_domain >= compute_group_id_domain(value));
  return *optional_group_id_domain;
}

namespace transform {

group_id_mapping_and_group_id_domain case_id_column_to_mapping_and_group_id_domain(
    const column_t& case_id_column, const common::execution_context& ctx) {
  const auto sub_ctx{ctx.create_sub_context("case_id_column_to_mapping_and_group_id_domain", {})};
  static constexpr size_t CASE_AGGREGATION_GRAIN_SIZE{1 << 17};  // Same as in MO_BPMN_GRAPH and others
  const common::case_aligned_range range{case_id_column, sub_ctx, CASE_AGGREGATION_GRAIN_SIZE};

  return memory::cast_execute_column_pointers(
      [&range](const auto& tuple) -> group_id_mapping_and_group_id_domain {
        const auto case_id_acc{std::get<0>(tuple).get_const_accessor()};
        using case_col_ptr_t = typename decltype(case_id_acc)::type;
        using group_id_32_t = std::remove_const_t<join_projection32_t::value_type>;
        using group_id_64_t = std::remove_const_t<join_projection64_t::value_type>;
        // If the width of case_col_ptr_t is less than or equal to group_id_32_t, 32bit suffice. Otherwise, use 64bit.
        using group_id_t = std::common_type_t<case_col_ptr_t, group_id_32_t>;
        static_assert(ctl::one_of<group_id_t, group_id_32_t, group_id_64_t>);

        safe_max_int<group_id_t> max_case_col_ptr{};
        const auto column_size{range.end};
        auto group_id_mapping{ctl::make_shared_static_array_for_overwrite<group_id_t>(
            column_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

        tbb::parallel_for(range, [&case_id_acc = std::as_const(case_id_acc), &max_case_col_ptr,
                                  &group_id_mapping](const auto& range) {
          group_id_t local_max_case_col_ptr{-1};
          for (auto row_idx{range.begin}; row_idx < range.end; ++row_idx) {
            const group_id_t case_id_col_ptr{case_id_acc[row_idx]};
            //            debug_assert(case_id_col_ptr != 0);
            local_max_case_col_ptr = std::max(local_max_case_col_ptr, case_id_col_ptr);
            group_id_mapping[row_idx] = case_id_col_ptr;
          }
          max_case_col_ptr.update(local_max_case_col_ptr);
        });
        return {.value = std::move(group_id_mapping), .optional_group_id_domain = max_case_col_ptr.get() + 1};
      },
      case_id_column->get_column_pointers(ctx));
}

}  // namespace transform

}  // namespace celonis::accelerator::memory
