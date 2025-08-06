#include "sr_trace_accessor.h"

#include <cpml/constants.h>

#include "exprs/celonis/id.h"
#include "gutil/casts.h"

namespace starrocks::celonis::cpml_utils {

sr_trace_accessor::sr_trace_accessor(const UnnestedArrayData& array_data)
        : activities_{*down_cast<const RunTimeColumnType<TYPE_VARCHAR>*>(array_data.elements)} {
    const auto& [elements, offsets, null_arrays, null_elements]{array_data};
    auto activity_ids{ctl::make_static_array_for_overwrite<cpml::activity_id_t>(elements->size(),
                                                                                ALLOC_MSG(ctl::MEMBER_INIT_MSG))};

    const auto is_null_element{[null_elements](const std::size_t element_idx) {
        return null_elements != nullptr && (*null_elements)[element_idx] != 0;
    }};

    // Assign activity IDs for all null activity names by either hashing or assigning zero for NULL values
    for (size_t elements_idx{0}; elements_idx < activity_ids.size(); ++elements_idx) {
        activity_ids[elements_idx] =
                is_null_element(elements_idx) ? cpml::NULL_ACTIVITY_ID : Id::get(activities_.get_slice(elements_idx));
    }

    // Because the offsets array always includes the end offset as well, it has one more offset than there are traces
    const auto number_of_traces{offsets->size() - 1};
    auto trace_offsets{
            ctl::make_static_array_for_overwrite<index_type>(number_of_traces, ALLOC_MSG(ctl::MEMBER_INIT_MSG))};
    auto trace_views{
            ctl::make_static_array<cpml::event_data::trace_view>(number_of_traces, ALLOC_MSG(ctl::MEMBER_INIT_MSG))};

    if (number_of_traces > 0) {
        // For the given trace index, produces a trace view and returns it together with its begin offset
        const auto make_trace_view_for_trace_idx{
                [&](const std::size_t trace_idx) -> std::pair<std::size_t, cpml::event_data::trace_view> {
                    DCHECK(trace_idx < number_of_traces);
                    const auto is_null_array{[null_arrays](const std::size_t trace_idx) {
                        return null_arrays != nullptr && (*null_arrays)[trace_idx] != 0;
                    }};
                    static constexpr cpml::event_data::trace_view EMPTY_OR_NULL_TRACE{};

                    const auto& offsets_data{offsets->get_data()};
                    const ctl::array_view activity_ids_data_view{activity_ids};
                    const auto trace_start_idx{offsets_data.at(trace_idx)};
                    const auto trace_end_idx{offsets_data.at(trace_idx + 1)};
                    const auto trace_length{trace_end_idx - trace_start_idx};
                    const auto trace_view{is_null_array(trace_idx) || trace_length == 0
                                                  ? EMPTY_OR_NULL_TRACE
                                                  : activity_ids_data_view.sub_view(trace_start_idx, trace_length)};
                    return {trace_start_idx, trace_view};
                }};

        // For each trace offset, we create a view to the activity IDs and transform the offset to the internal type
        for (std::size_t trace_idx{0}; trace_idx < number_of_traces; ++trace_idx) {
            const auto [trace_start_idx, trace_view]{make_trace_view_for_trace_idx(trace_idx)};
            trace_offsets.at(trace_idx) = trace_start_idx;
            trace_views.at(trace_idx) = trace_view;
        }
    }

    activity_ids_ = std::move(activity_ids);
    trace_offsets_ = std::move(trace_offsets);
    trace_views_ = std::move(trace_views);
}

sr_trace_accessor::size_type sr_trace_accessor::total_row_count() const {
    return activity_ids_.size();
}

cpml::event_data::trace_views_t sr_trace_accessor::trace_views() const {
    return trace_views_;
}

ctl::array_view<const sr_trace_accessor::index_type> sr_trace_accessor::trace_offsets() const {
    return trace_offsets_;
}

cpml::optional_activity_id_t sr_trace_accessor::activity_id_for_name(
        const cpml::activity_name_view_t activity_name) const {
    return Id::get(Slice{activity_name.data(), activity_name.size()});
}

cpml::activity_name_view_t sr_trace_accessor::activity_name_for_row_index(const std::size_t row_index) const {
    return activities_.get_slice(row_index); // Use conversion operator to std::string_view
}

} // namespace starrocks::celonis::cpml_utils
