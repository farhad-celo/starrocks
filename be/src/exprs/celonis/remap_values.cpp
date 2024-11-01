#include "exprs/celonis/remap_values.h"

#include "column/array_column.h"
#include "column/column_hash.h"
#include "column/column_helper.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

template<LogicalType LT, typename = guard::Guard>
struct ValueMap {
};

template<LogicalType LT>
struct ValueMap<LT, FixedLengthLTGuard<LT>> {
    using CppType = RunTimeCppValueType<LT>;
    using KeyType = CppType;
    using HashMap = phmap::flat_hash_map<KeyType, Datum, StdHash<CppType>>;
};

template<LogicalType LT>
struct ValueMap<LT, StringLTGuard<LT>> {
    using CppType = RunTimeCppValueType<LT>;
    using KeyType = std::string;
    using HashMap = phmap::flat_hash_map<KeyType, Datum, SliceHash>;
};

template<LogicalType LT>
struct RemapValuesStateFragmentLocal {
    using ValueHashMap = typename ValueMap<LT>::HashMap;
    using CppType = RunTimeCppType<LT>;
    using HashMapKeyType = typename ValueMap<LT>::KeyType;

    ValueHashMap value_map;
    ScalarFunction function;
    std::optional<Datum> null_value = std::nullopt;

    void insert(const Datum& old_datum, const Datum& new_datum) {
        if (old_datum.is_null()) {
            null_value = new_datum;
        } else {
            const auto key = _convert_to_key_type(old_datum.get<CppType>());
            value_map[key] = new_datum;
        }
    }

    Datum get(const Datum& key_datum, const std::optional<Datum>& default_value) const {
        if (key_datum.is_null()) {
            if (null_value.has_value()) {
                return null_value.value();
            }
            return default_value.has_value() ? default_value.value() : key_datum;
        }
        const auto key = _convert_to_key_type(key_datum.get<CppType>());
        auto it = value_map.find(key);
        if (it == value_map.end()) {
            return default_value.has_value() ? default_value.value() : key_datum;
        } else {
            return it->second;
        }
    }

private:
    HashMapKeyType _convert_to_key_type(CppType v) const {
        if constexpr (lt_is_string<LT>) {
            return std::string(v.data, v.size);
        } else {
            return v;
        }
    }
};

template<LogicalType LT>
Status prepare_helper(FunctionContext* context, FunctionContext::FunctionStateScope
scope, StatusOr<ColumnPtr> (* func_const)(FunctionContext*, const starrocks::Columns&),
                      StatusOr<ColumnPtr>(* func_general)(FunctionContext*, const starrocks::Columns&)) {
    using CppType = RunTimeCppValueType<LT>;
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new RemapValuesStateFragmentLocal<LT>();
    context->set_function_state(scope, state);

    auto old_value_column = context->get_constant_column(1);
    auto new_value_column = context->get_constant_column(2);
    // context->is_constant_column(i) must not be used to determine if the argument is Array Literal because as of
    // 2024-01-30 it returns false for Array Literal while get_constant_column(i) returns non nullptr.
    // For the same reason, columns[i]->is_constant() must not be used in celonis_remap_values().
    if (old_value_column == nullptr || new_value_column == nullptr) {
        state->function = func_general;
        return Status::OK();
    }
    state->function = func_const;

    if (old_value_column->is_null(0) || new_value_column->is_null(0)) {
        return Status::OK();
    }

    auto old_value_array = old_value_column->get(0).get_array();
    auto new_value_array = new_value_column->get(0).get_array();
    if (old_value_array.size() != new_value_array.size()) {
        return Status::InvalidArgument("[prepare] old value array must have the same length as new value array.");
    }
    const auto size = old_value_array.size();
    for (auto i = 0; i < size; ++i) {
        state->insert(old_value_array[i], new_value_array[i]);
    }
    return Status::OK();
}

template<LogicalType LT>
Status CelonisRemapValues<LT>::prepare_const(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return prepare_helper<LT>(context, scope, remap_values_constant_value_map,
                              remap_values_const_non_constant_value_map);
}

template<LogicalType LT>
Status CelonisRemapValues<LT>::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return prepare_helper<LT>(context, scope, remap_values_constant_value_map, remap_values_non_constant_value_map);
}

template<LogicalType LT>
Status CelonisRemapValues<LT>::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal<LT>*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisRemapValues<LT>::remap_values_const_non_constant_value_map([[maybe_unused]]FunctionContext* context,
                                                                  const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& old_value_column = columns[1];
    const auto& new_value_column = columns[2];
    const bool has_default = columns.size() == 4;
    auto num_rows = value_column->size();

    auto unfolded_value_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_value_column->clone_empty());

    if (num_rows == 0) {
        return result;
    }

    // Assume old_value_array and new_value_array columns are constant, use the first row to construct the map.
    auto old_value_datum = old_value_column->get(0);
    auto new_value_datum = new_value_column->get(0);
    if (old_value_datum.is_null() || new_value_datum.is_null()) {
        result->append_nulls(num_rows);
        return result;
    }
    const auto& old_value_array = old_value_datum.get_array();
    const auto& new_value_array = new_value_datum.get_array();
    if (old_value_array.size() != new_value_array.size()) {
        return Status::InvalidArgument("old value array must have the same length as new value array.");
    }
    const auto size = old_value_array.size();
    auto state = RemapValuesStateFragmentLocal<LT>();
    for (auto i = 0; i < size; ++i) {
        state.insert(old_value_array[i], new_value_array[i]);
    }

    for (int row = 0; row < num_rows; ++row) {
        auto value = value_column->get(row);
        result->append_datum(
                state.get(value, has_default ? std::optional<Datum>(columns[3]->get(row)) : std::nullopt));
    }
    return result;
}

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisRemapValues<LT>::remap_values_non_constant_value_map([[maybe_unused]]FunctionContext* context,
                                                            const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& old_value_column = columns[1];
    const auto& new_value_column = columns[2];
    const bool has_default = columns.size() == 4;
    auto num_rows = value_column->size();

    auto unfolded_value_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_value_column->clone_empty());

    for (int row = 0; row < num_rows; ++row) {
        auto old_value_datum = old_value_column->get(row);
        auto new_value_datum = new_value_column->get(row);
        if (old_value_datum.is_null() || new_value_datum.is_null()) {
            result->append_nulls(1);
            continue;
        }
        const auto& old_value_array = old_value_datum.get_array();
        const auto& new_value_array = new_value_datum.get_array();
        if (old_value_array.size() != new_value_array.size()) {
            return Status::InvalidArgument("old value array must have the same length as new value array.");
        }
        const auto value = value_column->get(row);
        const auto value_key = value.convert2DatumKey();
        const int size = static_cast<int>(old_value_array.size());
        bool found = false;
        // Note that if a value is remapped in multiple pairs, only the last pair is used. So we traverse the remap
        // pairs reversely.
        for (int i = size - 1; i >= 0; --i) {
            if (value_key == old_value_array[i].convert2DatumKey()) {
                found = true;
                result->append_datum(new_value_array[i]);
                break;
            }
        }
        if (!found) {
            if (has_default) {
                result->append_datum(columns[3]->get(row));
            } else {
                result->append_datum(value);
            }
        }
    }
    return result;
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisRemapValues<LT>::remap_values_constant_value_map([[maybe_unused]]FunctionContext* context,
                                                                            const Columns& columns) {
    const auto& value_column = columns[0];
    const bool has_default = columns.size() == 4;
    auto unfolded_value_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_value_column->clone_empty());
    const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto num_rows = value_column->size();
    for (int row = 0; row < num_rows; ++row) {
        auto value = value_column->get(row);
        result->append_datum(
                state->get(value, has_default ? std::optional<Datum>(columns[3]->get(row)) : std::nullopt));
    }
    return result;
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisRemapValues<LT>::remap_values(FunctionContext* context, const Columns& columns) {
    DCHECK(columns.size() == 3 || columns.size() == 4);
    const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisRemapValues<LT>::remap_values_const(FunctionContext* context, const Columns& columns) {
    DCHECK(columns.size() == 3 || columns.size() == 4);
    const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template
class CelonisRemapValues<TYPE_BIGINT>;

template
class CelonisRemapValues<TYPE_DOUBLE>;

template
class CelonisRemapValues<TYPE_DATETIME>;

template
class CelonisRemapValues<TYPE_VARCHAR>;


} // namespace starrocks
