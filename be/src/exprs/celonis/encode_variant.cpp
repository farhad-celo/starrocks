#include "exprs/celonis/encode_variant.h"

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

class ActivityMap {
public:
    ActivityMap() = default;

    explicit ActivityMap(const DatumArray& array) {
        for (const auto& activity: array) {
            if (activity.is_null()) {
                continue;
            }
            map_.try_emplace(activity.get_slice(), map_.size());
        }
    }

    int32_t get(const Slice& activity) const {
        auto it = map_.find(activity);
        return it != map_.end() ? it->second : -1;
    }

private:
    phmap::flat_hash_map<Slice, int32_t, SliceHashWithSeed<PhmapSeed1>, SliceEqual> map_;
};

struct EncodeVariantStateFragmentLocal {
    ActivityMap activity_map;
    bool null_map = false;
    ScalarFunction function;
};
}

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
    state->activity_map = ActivityMap(activity_array_column->get(0).get_array());
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
        ActivityMap activity_map(columns[1]->get(row).get_array());
        // encode variant
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        size_t count = 0;
        for (auto i = start; i < end; ++i) {
            if (variant_data.null_elements != nullptr && (*variant_data.null_elements)[i] != 0) {
                continue;
            }
            ++count;
            array_index_column->append(activity_map.get(activities[i]));
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
            array_index_column->append(state->activity_map.get(activities[i]));
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
