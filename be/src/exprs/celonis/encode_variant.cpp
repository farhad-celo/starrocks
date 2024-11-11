#include "exprs/celonis/encode_variant.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

struct EncodeVariantStateFragmentLocal {
    phmap::flat_hash_map<std::string, int32_t, StdHash<std::string>> activity_map;
    bool null_map = false;
    ScalarFunction function;

    void insert(const std::string& activity) {
        activity_map.try_emplace(activity, activity_map.size());
    }

    int32_t get(const std::string& activity) const {
        auto it = activity_map.find(activity);
        return it != activity_map.end() ? it->second : -1;
    }
};

Status CelonisEncodeVariant::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new EncodeVariantStateFragmentLocal();
    context->set_function_state(scope, state);

    auto activity_array_column = context->get_constant_column(1);
    if (activity_array_column == nullptr) {
        state->function = encode_variant_non_constant_map;
        // TODO(y.zhang): Consider turn on this.
        if (false && config::fail_query_when_expensive_non_const_impl_is_called) {
            return Status::InvalidArgument("The non-const version of CELONIS_ENCODE_VARIANT should not be called.");
        }
        return Status::OK();
    }
    state->function = encode_variant_constant_map;

    if (activity_array_column->is_null(0)) {
        state->null_map = true;
        return Status::OK();
    }

    state->null_map = false;
    auto activity_array = activity_array_column->get(0).get_array();
    for (const auto& activity: activity_array) {
        if (activity.is_null()) {
            continue;
        } else {
            state->insert(activity.get_slice().to_string());
        }
    }

    return Status::OK();
}

Status CelonisEncodeVariant::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const EncodeVariantStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisEncodeVariant::encode_variant_non_constant_map([[maybe_unused]]FunctionContext* context,
                                                                          const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnPtr variant_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData variant_data = prepare_array_input(variant_column.get());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *variant_data.elements).get_data().data();
    const auto& offsets = variant_data.offsets->get_data().data();

    Int32Column::Ptr array_index_column = Int32Column::create();
    auto null_column = NullColumn::create();
    int offset = 0;
    UInt32Column::Ptr array_offsets = UInt32Column::create();
    array_offsets->reserve(num_rows + 1);

    for (auto row = 0; row < num_rows; ++row) {
        array_offsets->append(offset);
        if (variant_column->is_null(row) || columns[1]->is_null(row)) {
            null_column->append(1);
            continue;
        }
        null_column->append(0);
        // construct activity_map
        phmap::flat_hash_map<std::string, int32_t, StdHash<std::string>> activity_map;
        auto activity_array = columns[1]->get(row).get_array();
        for (const auto& activity_datum: activity_array) {
            if (activity_datum.is_null()) {
                continue;
            } else {
                activity_map.try_emplace(activity_datum.get_slice().to_string(), activity_map.size());
            }
        }
        // encode variant
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        size_t count = 0;
        for (auto i = start; i < end; ++i) {
            if (variant_data.null_elements != nullptr && (*variant_data.null_elements)[i] != 0) {
                continue;
            }
            ++count;
            auto it = activity_map.find(activities[i].to_string());
            auto idx = (it != activity_map.end() ? it->second : -1);
            array_index_column->append(idx);
        }
        offset += count;
    }
    array_offsets->append(offset);
    return NullableColumn::create(
            ArrayColumn::create(NullableColumn::create(array_index_column, NullColumn::create(offset, 0)),
                                array_offsets), null_column);

}

StatusOr<ColumnPtr>
CelonisEncodeVariant::encode_variant_constant_map([[maybe_unused]]FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnPtr variant_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData variant_data = prepare_array_input(variant_column.get());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *variant_data.elements).get_data().data();
    const auto& offsets = variant_data.offsets->get_data().data();

    const auto* state = reinterpret_cast<const EncodeVariantStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    Int32Column::Ptr array_index_column = Int32Column::create();
    auto null_column = NullColumn::create();
    int offset = 0;
    UInt32Column::Ptr array_offsets = UInt32Column::create();
    array_offsets->reserve(num_rows + 1);

    for (auto row = 0; row < num_rows; ++row) {
        array_offsets->append(offset);
        if (variant_column->is_null(row) || state->null_map) {
            null_column->append(1);
            continue;
        }
        null_column->append(0);
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        size_t count = 0;
        for (auto i = start; i < end; ++i) {
            if (variant_data.null_elements != nullptr && (*variant_data.null_elements)[i] != 0) {
                continue;
            }
            ++count;
            array_index_column->append(state->get(activities[i].to_string()));
        }
        offset += count;
    }
    array_offsets->append(offset);
    return NullableColumn::create(
            ArrayColumn::create(NullableColumn::create(array_index_column, NullColumn::create(offset, 0)),
                                array_offsets), null_column);

}

StatusOr<ColumnPtr> CelonisEncodeVariant::encode_variant(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const EncodeVariantStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}


} // namespace starrocks
