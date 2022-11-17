#include "exprs/celonis/match_activities.h"

#include "column/array_column.h"
#include "column/hash_set.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

template<bool has_null>
ColumnPtr _celonis_match_activities_impl(FunctionContext* context, const Column& elements, const UInt32Column& offsets,
                                         const NullColumn::Container* null_offsets, const SliceHashSet& nodes) {
    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_column = BooleanColumn::create(num_array, 0);
    unsigned char *result_data = result_column->get_data().data();
    using ValueType = RunTimeCppType<TYPE_VARCHAR>;
    auto elements_ptr = (const ValueType *) (elements.raw_data());
    // Collects the nodes that pass any of the 'NODES' filter.
    SliceHashSet passing_nodes;
    passing_nodes.reserve(nodes.size());

    for (size_t i = 0; i < num_array; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        passing_nodes.clear();

        for (size_t index = 0; index < array_size; ++index) {
            if constexpr (has_null) {
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
            result_data[i] = 1;
        }
    }
    return result_column;
}
} // namespace

StatusOr<ColumnPtr> CelonisMatchActivitiesFunctions::celonis_match_activities(FunctionContext* context, const Columns& columns) {
    if (columns[0]->has_null()) {
        std::stringstream error;
        error << "unexpected null activities array as input to celonis_match_activities" << std::endl;
        context->set_error(error.str().c_str());
        return BooleanColumn::create(columns[0]->size(), 0);
    }

    const auto& array_column = extract_array_column(columns[0].get());
    const UInt32Column& offsets = array_column.offsets();
    const Column* elements = &array_column.elements();
    const NullColumn::Container* null_elements = nullptr;

    bool has_null = elements->has_null();
    if (has_null) {
        null_elements = &(down_cast<const NullableColumn*>(elements)->null_column()->get_data());
    }

    if (auto nullable = dynamic_cast<const NullableColumn*>(elements); nullable != nullptr) {
        elements = nullable->data_column().get();
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

    if (has_null) {
        _celonis_match_activities_impl<true>(context, *elements, offsets, null_elements, nodes);
    }
    return _celonis_match_activities_impl<false>(context, *elements, offsets, null_elements, nodes);
}

} // namespace starrocks
