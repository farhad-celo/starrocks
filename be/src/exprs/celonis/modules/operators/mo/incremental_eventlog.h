#pragma once

#include "legacy_embedded_ctl/static_array.h"
#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::mo {

/**
 * Given an event-log, create a ranking of its variants. We can then build filters that only consider the "top N"
 * variants of the event-log.
 */
class incremental_eventlog {
 public:
  /**
   * Create an incremental_eventlog for which the variants are decreasing in frequency.
   *
   * There is no guarantee on the order of variants with the same frequency.
   * @param activity_column The event-log of activities. Is expected to be grouped by case
   * @param case_column The respective case identifier for each activity of the event-log (activity_column)
   * @param filter An initial filter, if you want to mask some of the rows
   * @param context The execution context we are running in
   * @return
   */
  static incremental_eventlog frequency_decreasing(const memory::column_t& activity_column,
                                                   memory::column_t case_column, cube::filter_bitset_t filter,
                                                   const common::execution_context& context);

  /**
   * Given a set of chosen activities, create an incremental_eventlog where the top variants form a minimal cover of
   * that activity set. The other variants are then ordered by "similarity to the covering" and frequency count as a
   * secondary sorting criterion.
   *
   * We measure "similarity [of a variant] to the covering" here by the minimum cardinality of the symmetric set
   * differences between the variant and the variants in the covering.
   * There is no guarantee on the order of variants with the same "similarity" and frequency count.
   * @param activity_column The event-log of activities. Is expected to be grouped by case
   * @param case_column The respective case identifier for each activity of the event-log (activity_column)
   * @param filter An initial filter, if you want to mask some of the rows
   * @param context The execution context we are running in
   * @return
   */
  static std::pair<incremental_eventlog, size_t> activity_cover(const std::vector<row_id>& activity_ids,
                                                                const memory::column_t& activity_column,
                                                                memory::column_t case_column,
                                                                cube::filter_bitset_t filter,
                                                                const common::execution_context& context = {});
  /**
   * The incremental_eventlog can be thought of as a lazy range of filters. This is the size of this range.
   * @return the number of variants in the event-log
   */
  [[nodiscard]] row_id variant_count() const noexcept {
    return static_cast<row_id>(accumulated_variant_counts_.size());
  }

  [[nodiscard]] row_id object_count() const noexcept {
    return accumulated_variant_counts_.empty() ? 0 : accumulated_variant_counts_.back();
  }

  struct retained_objects {
    cube::filter_bitset_t filter;
    row_id count;
  };
  /**
   * Create a filter that only considers the num_variants most frequent variants, respecting the original filter.
   * @param num_variants The number of variants that the filter shall consider. Values outside of [0, variant_count()]
   * are clipped to that interval
   * @param context the execution context
   * @return the bit-wise 'or' of the original filter and a filter ignoring all but the num_variants most frequent
   * variants
   */
  [[nodiscard]] retained_objects retain_most_frequent(row_id num_variants,
                                                      const common::execution_context& context) const;

  /**
   * Calculates the minimal required number of variants to obtain an event log with a specified ratio of objects.
   * @param ratio The ratio of objects that should be retained. Values outside of [0, 1] are clipped to that
   * interval
   * @return number of retained variants
   */
  [[nodiscard]] row_id retain_percentage_of_objects(double ratio) const;

 private:
  incremental_eventlog(memory::column_t case_column, cube::filter_bitset_t filter,
                       legacy_embedded_ctl::shared_static_array<row_id> ranking,
                       legacy_embedded_ctl::shared_static_array<row_id> accumulated_variant_counts)
      : case_column_{std::move(case_column)},
        initial_filter_{std::move(filter)},
        ranking_{std::move(ranking)},
        accumulated_variant_counts_{std::move(accumulated_variant_counts)} {}
  memory::column_t case_column_;
  cube::filter_bitset_t initial_filter_;
  legacy_embedded_ctl::shared_static_array<row_id> ranking_;
  legacy_embedded_ctl::shared_static_array<row_id> accumulated_variant_counts_;
};

}  // namespace celonis::accelerator::operators::mo
