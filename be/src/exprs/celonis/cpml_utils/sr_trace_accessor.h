#pragma once

#include <cpml/event_data/trace_accessor.h>
#include <cpml/event_data/trace_view.h>
#include <cpml/types.h>
#include <ctl/array_view.h>
#include <ctl/static_array.h>

#include "column/type_traits.h"
#include "exprs/celonis/util.h"
#include "types/logical_type.h"

namespace starrocks::celonis::cpml_utils {

/** The trace accessor must support mapping an activity name to an ID and a row index to an activity name */
using trace_accessor_base =
        cpml::event_data::trace_accessor_with_activity_name_to_id_mapping_and_with_index_to_activity_name_mapping;

class sr_trace_accessor final : public trace_accessor_base {
public:
    /**
      * @brief Constructs a trace_accessor from an activity array column
      * @post Does not take ownership of the referenced activities in array_data. That is, as long as this instance
      * exists, the elements pointed to in array_data must be valid. Otherwise, it would lead to a dangling reference
      *
      * @note Some general overview w.r.t the input data structure. The UnnestedArrayData represents a proxy around an
      * array column which is easier to work with. As an example, assume we have the following activity table including
      * the activity array column as input:
      *
      * +---------+--------------------+
      * | Case ID |   Activity Array   |
      * +---------+--------------------+
      * |       1 | [A, B, C]          |
      * |       2 | [NULL, A]          |
      * |       3 | [NULL, NULL, NULL] |
      * |       4 | []                 |
      * |       5 | [A]                |
      * |       6 | NULL               |
      * |       7 | [NULL]             |
      * +---------+--------------------+
      *
      * The given 'array_data' encodes this activity array column using 4 members:
      * - elements: Contains the raw activity array elements EXCLUDING null values.
      *   - In the example, [A, B, C, A, A]
      *   - Size: 5
      * - offsets: Contains the trace start/end offsets into a flat array view INCLUDING null values
      *   - In the example, [0, 3, 5, 8, 8, 9, 9, 10]
      *   - Size: 9 (note: the last offset values refers to past the last element)
      * - null_arrays: Encodes which entire trace is considered null (not the same as being empty)
      *   - In the example, only trace with indexes/offset 5 (i.e., case 6) is null
      *   - null_arrays bit-array: [0, 0, 0, 0, 0, 1, 0]
      *   - Size: 7
      * - null_elements: Encodes which elements within a trace is considered null
      *   - In the example, elements with indexes/offsets 3, 5, 6, 7, 9 are null
      *   - null_elements bit-array: [0, 0, 0, 1, 0, 1, 1, 1, 0, 1]
      *   - Size: 10
      */
    explicit sr_trace_accessor(const UnnestedArrayData& array_data);

    [[nodiscard]] size_type total_row_count() const override;
    [[nodiscard]] cpml::event_data::trace_views_t trace_views() const override;
    [[nodiscard]] ctl::array_view<const index_type> trace_offsets() const override;
    [[nodiscard]] cpml::optional_activity_id_t activity_id_for_name(
            cpml::activity_name_view_t activity_name) const override;
    [[nodiscard]] cpml::activity_name_view_t activity_name_for_row_index(std::size_t row_index) const override;

private:
    const RunTimeColumnType<TYPE_VARCHAR>& activities_;
    ctl::static_array<cpml::activity_id_t> activity_ids_{};
    // TODO(n.weber): We could think about making this accessor class templated for the offset type s.t. no
    //   transformation from e.g., uint32 to size_t is necessary
    ctl::static_array<index_type> trace_offsets_{};
    ctl::static_array<cpml::event_data::trace_view> trace_views_{};
};

} // namespace starrocks::celonis::cpml_utils
