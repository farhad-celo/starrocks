#pragma once

#include <tbb/parallel_for.h>

#include <ctl/static_array.h>
#include <cpml/variant/types_and_constants.h>

#include "modules/common/execution_context.h"
#include "modules/common/trace_types.h"
#include "modules/memory/cache/variant_trace_cache.h"

namespace celonis::accelerator::memory::cache {

template <typename From, typename To>
concept non_narrowing_conversion = requires(From f) { To{f}; };

template <typename T>
concept valid_variant_element_type = std::signed_integral<T> && non_narrowing_conversion<trace_element_type, T>;

template <valid_variant_element_type T>
using variant_t = ctl::static_array<T>;

template <valid_variant_element_type T>
using variants_t = ctl::static_array<variant_t<T>>;

/** Transforms the given variants representation to a vec<vec<T>> (parallelized) */
template <valid_variant_element_type T, typename REMAP_FUNC_T>
[[nodiscard]] variants_t<T> remap_variants(const variant_trace_cache& variants, const REMAP_FUNC_T remap_func,
                                           const common::execution_context& ctx) {
  const auto sub_ctx{ctx.create_sub_context("remap_variants", {})};

  const auto number_of_variants{variants.get_num_traces()};
  auto type_mapped_variants(ctl::make_static_array<variant_t<T>>(ctl::cast_unsigned(number_of_variants),
                                                                 ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG)));

  using cpml::variant::variant_count_t;
  using variant_id_t = variant_trace_cache::variant_id_t;
  using raw_variant_id_t = variant_id_t::UnderlyingType;

  tbb::this_task_arena::isolate([&] {
    tbb::parallel_for(tbb::blocked_range<variant_count_t>{0, number_of_variants},
                      [&type_mapped_variants, &variants = std::as_const(variants),
                       &remap_func = std::as_const(remap_func), &sub_ctx = std::as_const(sub_ctx)](const auto& range) {
                        for (raw_variant_id_t variant_id{range.begin()}; variant_id < range.end(); ++variant_id) {
                          const auto variant_view{variants.variant_view_for_id(variant_id_t{variant_id}, sub_ctx)};
                          // TODO(n.weber): In case we still have an overhead for small static array allocations, we
                          // should simply use std::vector
                          auto remapped_variant{ctl::make_static_array_for_overwrite<T>(
                              variant_view.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
                          // TODO(n.weber): Support for static arrays in ctl::transform_to would be very convenient
                          std::ranges::transform(variant_view, remapped_variant.begin(), remap_func);
                          type_mapped_variants.at(variant_id) = std::move(remapped_variant);
                        }
                      });
  });

  return type_mapped_variants;
}

}  // namespace celonis::accelerator::memory::cache
