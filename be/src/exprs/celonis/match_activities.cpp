#include "exprs/celonis/match_activities.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/hash_set.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {

template<bool element_has_null>
ColumnPtr _celonis_match_activities_impl(FunctionContext* context, const Column& elements, const UInt32Column& offsets,
                                         const NullColumn::Container* null_offsets,
                                         const NullColumn::Container* activity_array_nulls,
                                         const SliceHashSet& nodes) {
    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(num_array);
    result.reserve(num_array);

    using ValueType = RunTimeCppType<TYPE_VARCHAR>;
    auto elements_ptr = (const ValueType *) (elements.raw_data());
    // Collects the nodes that pass any of the 'NODES' filter.
    SliceHashSet passing_nodes;
    passing_nodes.reserve(nodes.size());

    for (size_t i = 0; i < num_array; i++) {
        if (activity_array_nulls != nullptr && (*activity_array_nulls)[i]) {
            result.append_null();
            continue;
        }

        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        passing_nodes.clear();

        for (size_t index = 0; index < array_size; ++index) {
            if constexpr (element_has_null) {
                // Nulls are ignored
                if ((*null_offsets)[offset + index] != 0) {
                    continue;
                }
            }
            const auto &value = elements_ptr[offset + index];
            if (nodes.count(value)) {
                passing_nodes.insert(value);
                if (passing_nodes.size() == nodes.size()) {
                    // All nodes in the filter have been matched.
                    break;
                }
            }
        }

        if (passing_nodes.size() == nodes.size()) {
            result.append(1L);
        } else {
            result.append(0L);
        }
    }
    return result.build(/*is_const=*/false);
}
} // namespace

StatusOr<ColumnPtr> CelonisMatchActivitiesFunctions::celonis_match_activities(FunctionContext* context, const Columns& columns) {
    const Column* activity_array =  columns[0].get();
    const NullableColumn* nullable_activity_array = nullptr;
    const NullColumn::Container* activity_array_nulls = nullptr;

    if (activity_array->is_nullable()) {
        nullable_activity_array = down_cast<const NullableColumn*>(activity_array);
        activity_array = nullable_activity_array->data_column().get();
        activity_array_nulls = &(nullable_activity_array->null_column()->get_data());
    }

    const auto& activity_array_column = extract_array_column(activity_array);
    const UInt32Column& activity_offsets = activity_array_column.offsets();
    const Column* activity_elements = &activity_array_column.elements();
    const NullColumn::Container* activity_nulls = nullptr;
    if (activity_elements->has_null()) {
        activity_nulls = &(down_cast<const NullableColumn*>(activity_elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(activity_elements); nullable != nullptr) {
        activity_elements = nullable->data_column().get();
    }

    // schema:
    // columns[1] -- starting activities
    // columns[2] -- NODES
    // columns[3] -- ending activities
    // columns[4] -- exclude activities
    // columns[5] -- excluding any of specified activities
    // columns[6] -- NODES_ANY

    // TODO(a.gubichev): for now, only support flowing activities.
    if (columns[1]->get(0).get_array().size() != 0 ||
        columns[3]->get(0).get_array().size() != 0 ||
        columns[4]->get(0).get_array().size() != 0 ||
        columns[5]->get(0).get_array().size() != 0 ||
        columns[6]->get(0).get_array().size() != 0) {
        std::stringstream error;
        error << "unsupported filter in celonis_match_activities" << std::endl;
        throw std::runtime_error(error.str());
    }

    auto node_array_col =  columns[2]->get(0).get_array();
    SliceHashSet nodes;
    for (size_t i = 0; i < node_array_col.size(); ++i) {
        nodes.insert(node_array_col[i].get_slice());
    }

    if (activity_nulls != nullptr) {
        _celonis_match_activities_impl<true>(context, *activity_elements, activity_offsets, activity_nulls, activity_array_nulls, nodes);
    }
    return _celonis_match_activities_impl<false>(context, *activity_elements, activity_offsets, activity_nulls, activity_array_nulls, nodes);
}

} // namespace starrocks
