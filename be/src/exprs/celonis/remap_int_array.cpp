#include "exprs/celonis/remap_int_array.h"

#include "column/array_column.h"
#include "column/column_hash.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

struct IntValueHashMap {
    using HashMap = phmap::flat_hash_map<int64_t, int64_t, StdHash<int64_t>>;
    using NullSet = HashSet<int64_t>;

    HashMap value_map;
    NullSet null_mappings;                            // Keys that map to NULL
    std::optional<int64_t> null_value = std::nullopt; // value that NULL maps to

    IntValueHashMap() = default;

    explicit IntValueHashMap(const DatumArray& old_array, const DatumArray& new_array) {
        const auto size = old_array.size();
        for (auto i = 0; i < size; ++i) {
            const auto& old_datum = old_array[i];
            const auto& new_datum = new_array[i];
            if (old_datum.is_null()) {
                if (!new_datum.is_null()) {
                    null_value = new_datum.get_int64();
                }
            } else {
                if (new_datum.is_null()) {
                    null_mappings.insert(old_datum.get_int64()); // Track keys that map to null
                } else {
                    value_map[old_datum.get_int64()] = new_datum.get_int64();
                }
            }
        }
    }

    std::optional<int64_t> get(int64_t key, const std::optional<int64_t>& default_value, bool has_default) const {
        // Check if this key should map to null
        if (null_mappings.find(key) != null_mappings.end()) {
            return std::nullopt; // Return null
        }

        auto it = value_map.find(key);
        if (it == value_map.end()) {
            return has_default ? default_value : std::optional<int64_t>(key);
        } else {
            return it->second;
        }
    }

    std::optional<int64_t> get_null_mapping() const { return null_value; }
};

struct RemapIntArrayStateFragmentLocal {
    IntValueHashMap value_map;
    ScalarFunction function;
};

Status prepare_helper(FunctionContext* context, FunctionContext::FunctionStateScope scope,
                      StatusOr<ColumnPtr> (*func_const)(FunctionContext*, const starrocks::Columns&),
                      StatusOr<ColumnPtr> (*func_general)(FunctionContext*, const starrocks::Columns&)) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new RemapIntArrayStateFragmentLocal();
    context->set_function_state(scope, state);

    auto old_value_column = context->get_constant_column(1);
    auto new_value_column = context->get_constant_column(2);

    if (old_value_column == nullptr || new_value_column == nullptr) {
        state->function = func_general;
        return Status::OK();
    }
    state->function = func_const;

    if (old_value_column->is_null(0) || new_value_column->is_null(0)) {
        return Status::OK();
    }

    const DatumArray old_value_array = old_value_column->get(0).get_array();
    const DatumArray new_value_array = new_value_column->get(0).get_array();
    if (old_value_array.size() != new_value_array.size()) {
        return Status::InvalidArgument("[prepare] old value array must have the same length as new value array.");
    }
    state->value_map = IntValueHashMap(old_value_array, new_value_array);
    return Status::OK();
}

} // namespace

Status CelonisRemapIntArray::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return prepare_helper(context, scope, remap_int_array_constant_value_map, remap_int_array_non_constant_value_map);
}

Status CelonisRemapIntArray::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const RemapIntArrayStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisRemapIntArray::remap_int_array_non_constant_value_map(
        [[maybe_unused]] FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    const auto& input_array_column = columns[0];
    const auto& old_value_column = columns[1];
    const auto& new_value_column = columns[2];
    const bool has_default = columns.size() == 4;
    auto num_rows = input_array_column->size();

    auto unfolded_input_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_input_column->clone_empty());

    for (int row = 0; row < num_rows; ++row) {
        if (input_array_column->is_null(row)) {
            result->append_nulls(1);
            continue;
        }
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

        auto input_array_datum = input_array_column->get(row);
        const auto& input_array = input_array_datum.get_array();

        DatumArray result_array;
        result_array.reserve(input_array.size());
        std::optional<int64_t> default_val = std::nullopt;
        if (has_default && !columns[3]->is_null(row)) {
            default_val = columns[3]->get(row).get_int64();
        }

        for (const auto& element : input_array) {
            bool found = false;
            if (element.is_null()) {
                // Look for null mapping
                for (size_t i = 0; i < old_value_array.size(); ++i) {
                    if (old_value_array[i].is_null()) {
                        if (!new_value_array[i].is_null()) {
                            result_array.emplace_back(new_value_array[i].get_int64());
                        } else {
                            result_array.emplace_back(kNullDatum);
                        }
                        found = true;
                        break;
                    }
                }
            } else {
                // Look for matching value
                auto element_value = element.get_int64();
                for (size_t i = old_value_array.size(); i-- > 0;) { // Traverse reversely
                    if (!old_value_array[i].is_null() && old_value_array[i].get_int64() == element_value) {
                        if (!new_value_array[i].is_null()) {
                            result_array.emplace_back(new_value_array[i].get_int64());
                        } else {
                            result_array.emplace_back(kNullDatum);
                        }
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                if (has_default) {
                    result_array.emplace_back(default_val.has_value() ? default_val.value() : kNullDatum);
                } else {
                    result_array.emplace_back(element);
                }
            }
        }
        result->append_datum(DatumArray(std::move(result_array)));
    }
    return result;
}

StatusOr<ColumnPtr> CelonisRemapIntArray::remap_int_array_constant_value_map([[maybe_unused]] FunctionContext* context,
                                                                             const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    const auto& input_array_column = columns[0];
    const bool has_default = columns.size() == 4;
    auto unfolded_input_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_input_column->clone_empty());
    const auto* state = reinterpret_cast<const RemapIntArrayStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto num_rows = input_array_column->size();
    for (int row = 0; row < num_rows; ++row) {
        if (input_array_column->is_null(row)) {
            result->append_nulls(1);
            continue;
        }

        auto input_array_datum = input_array_column->get(row);
        const auto& input_array = input_array_datum.get_array();

        DatumArray result_array;
        result_array.reserve(input_array.size());

        std::optional<int64_t> default_val = std::nullopt;
        if (has_default && !columns[3]->is_null(row)) {
            default_val = columns[3]->get(row).get_int64();
        }

        for (const auto& element : input_array) {
            if (element.is_null()) {
                auto null_mapping = state->value_map.get_null_mapping();
                if (null_mapping.has_value()) {
                    result_array.emplace_back(null_mapping.value());
                } else {
                    result_array.emplace_back(default_val.has_value() ? default_val.value() : kNullDatum);
                }
            } else {
                auto mapped_value = state->value_map.get(element.get_int64(), default_val, has_default);
                if (mapped_value.has_value()) {
                    result_array.emplace_back(mapped_value.value());
                } else {
                    result_array.emplace_back(kNullDatum);
                }
            }
        }
        result->append_datum(DatumArray(std::move(result_array)));
    }
    return result;
}

StatusOr<ColumnPtr> CelonisRemapIntArray::remap_int_array(FunctionContext* context, const Columns& columns) {
    DCHECK(columns.size() == 3 || columns.size() == 4);
    const auto* state = reinterpret_cast<const RemapIntArrayStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
