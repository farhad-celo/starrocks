#pragma once

#include <vector>

#include "ctl/concepts.h"
#include "ctl/static_array.h"
#include "modules/cube/filter_bitset_fwd.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_config.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_fwd.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_types.h"
#include "parallel_stable_integer_sort_copy.h"

namespace celonis::accelerator::operators::process {

class splittable_eventlog {
 public:
  splittable_eventlog() = default;
  [[nodiscard]] static splittable_eventlog extract(const splittable_eventlog_config_t& extraction_config,
                                                   const common::execution_context& context);
  [[nodiscard]] static splittable_eventlog canonicalize_if_necessary(splittable_eventlog&& other, size_t extra_space,
                                                                     const common::execution_context& context);
  [[nodiscard]] activity_domain_count_t activity_domain_count() const noexcept { return activity_domain_count_; }
  [[nodiscard]] trace_domain_count_t trace_domain_count() const { return trace_domain_count_; }
  // TODO(a.swoboda) encapsulate the logic that requires an update of the case domain count instead of exposing this
  void set_trace_domain_count(row_id count) noexcept { trace_domain_count_ = trace_domain_count_t{count}; }
  [[nodiscard]] eventlog_view_t current_split_eventlog_view() const noexcept { return current_view_; }

  /**
   * Iterates over every trace in the current view and applies the given filter function to it. Sets the current view
   * as a continuous sequence of the filtered traces
   */
  template <typename FILTER_TRACE>
  void remove_if_per_trace(FILTER_TRACE filter_trace) {
    // Loop over every trace in current view
    std::visit(
        [filter_trace, this]<typename VIEW>(VIEW& view) {
          auto first{begin(view)};
          auto last{end(view)};
          for (auto next_trace{first}; next_trace != last;) {
            auto trace_start{next_trace};

            next_trace = std::ranges::next(
                std::adjacent_find(next_trace, last,
                                   [](auto lhs, auto rhs) { return lhs.trace_id_raw() != rhs.trace_id_raw(); }),
                1, last);

            auto new_trace_end{filter_trace(trace_start, next_trace)};
            for (; trace_start != new_trace_end; trace_start++) {
              *first++ = *trace_start;
            }

            if (trace_start == first) {  // no need to copy
              first = new_trace_end;
            } else {
              first = std::copy(trace_start, new_trace_end, first);
            }
          }
          current_view_ = VIEW{view.begin(), first};
        },
        current_view_);
  }

  /**
   * @brief Splits the current eventlog view according to the mapping and reorders the eventlog buffer accordingly.
   *
   * A short example might clarify things. Lets say we have the following set of traces:
   * Case 1: [A, B, C, D]
   * Case 2: [A, B, D]
   * Case 3: [A]
   *
   * The initial eventlog buffer is basically an array consisting of activity/case pairs. For the above traces, the
   * buffer would contain data represented by table (1) (see below). Now, the inductive miner cuts will yield a mapping
   * which partitions the eventlog "buffer" in table (1) into a disjoint set of eventlog "views". For example, the
   * mapping could be the following: {A -> 0, B -> 0, C -> 1, D -> 1}. This groups activities into a specific partition
   * (e.g., activity 'A' into group 0, 'B' into group 0, 'C' into group 1, ...). The split will do a stable sort on the
   * activities such that activities belonging to the same group are in the same partition. That is, 'split' for the
   * described mapping would yield two eventlog "views" which are represented by table (2) for group 0 and table (3) for
   * group 1 (see below). The underlying event log "buffer" would be the same as concatenating the two tables.
   *
   * * Table 1
   * +----------+---------+
   * | Activity | Case ID |
   * +----------+---------+    * Table 2
   * | A        |       1 |    +----------+---------+
   * | B        |       1 |    | Activity | Case ID |    * Table 3
   * | C        |       1 |    +----------+---------+    +----------+---------+
   * | D        |       1 |    | A        |       1 |    | Activity | Case ID |
   * | A        |       2 |    | B        |       1 |    +----------+---------+
   * | B        |       2 |    | A        |       2 |    | C        |       1 |
   * | D        |       2 |    | B        |       2 |    | D        |       1 |
   * | A        |       3 |    | A        |       3 |    | D        |       2 |
   * +----------+---------+    +----------+---------+    +----------+---------+
   */
  [[nodiscard]] std::vector<splittable_eventlog> split(const split_mapping_t& mapping);

 private:
  splittable_eventlog(activity_domain_count_t activity_domain_count, trace_domain_count_t case_domain_count,
                      eventlog_buffer_t eventlog, std::optional<eventlog_view_t> optional_current_view = std::nullopt);
  [[nodiscard]] static splittable_eventlog extract(const splittable_eventlog_config_for_using_entire_eventlog& config,
                                                   const common::execution_context& context);
  [[nodiscard]] static splittable_eventlog extract(const splittable_eventlog_config_for_using_variants& config,
                                                   const common::execution_context& context);
  void canonicalize_case_ids();
  /** Copies the event log buffer into a new one with the given column pointer size. */
  template <
      ctl::one_of<memory::col_ptr_8_t, memory::col_ptr_16_t, memory::col_ptr_32_t, memory::col_ptr_64_t> CASE_ID_TYPE>
  [[nodiscard]] splittable_eventlog copy(const common::execution_context& context) const;

  activity_domain_count_t activity_domain_count_{};
  trace_domain_count_t trace_domain_count_{};
  eventlog_buffer_t eventlog_{};
  eventlog_view_t current_view_{};
};

}  // namespace celonis::accelerator::operators::process
