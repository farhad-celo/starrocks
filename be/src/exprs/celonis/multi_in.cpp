#include "exprs/celonis/multi_in.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// Visitor to extract integer value
struct IntExtractor {
    template <typename T>
    std::optional<int64_t> operator()(const T& value) const {
        if constexpr (std::is_integral_v<T>) {
            return static_cast<int64_t>(value);
        } else {
            return std::nullopt;
        }
    }
};

// Visitor to extract floating-point value
struct FloatExtractor {
    template <typename T>
    std::optional<double> operator()(const T& value) const {
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<double>(value);
        } else {
            return std::nullopt;
        }
    }
};

bool equal(const DatumKey& var1, const DatumKey& var2) {
    if (var1.index() == var2.index()) {
        return var1 == var2;
    }
    // Cross-type comparison
    auto int1 = std::visit(IntExtractor{}, var1);
    auto int2 = std::visit(IntExtractor{}, var2);

    auto float1 = std::visit(FloatExtractor{}, var1);
    auto float2 = std::visit(FloatExtractor{}, var2);

    if (int1 && float2) {
        return static_cast<double>(*int1) == *float2;
    } else if (int2 && float1) {
        return static_cast<double>(*int2) == *float1;
    }
    return var1 == var2;
}

struct MatchLists {
    MatchLists() = default;

    MatchLists(const ColumnPtr& column, int row) {
        if (column->is_null(row)) {
            is_null = true;
        } else {
            auto& match_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column.get()))->fields();
            num_fields = match_fields.size();
            std::optional<size_t> n_tuples;
            for (auto j = 0; j < num_fields; ++j) {
                auto size = match_fields[j]->get(row).get_array().size();
                if (!n_tuples.has_value()) {
                    n_tuples = size;
                } else {
                    if (n_tuples.value() != size) {
                        is_length_inconsistent = true;
                        break;
                    }
                }
            }
            if (!is_length_inconsistent && n_tuples.has_value()) {
                keys_list.reserve(n_tuples.value());
                for (auto i = 0; i < n_tuples.value(); ++i) {
                    std::vector<DatumKey> keys;
                    keys.reserve(num_fields);
                    for (auto j = 0; j < num_fields; ++j) {
                        auto key = match_fields[j]->get(row).get_array()[i].convert2DatumKey();
                        keys.push_back(key);
                    }
                    keys_list.push_back(keys);
                }
            }
        }
    }

    bool match(const Columns& input_fields, int row) const {
        if (is_length_inconsistent) {
            return false;
        }
        const auto num_input_fields = input_fields.size();
        if (num_fields != num_input_fields) {
            return false;
        }
        std::vector<DatumKey> keys;
        for (auto j = 0; j < num_input_fields; ++j) {
            keys.push_back(input_fields[j]->get(row).convert2DatumKey());
        }
        for (const auto& match_keys : keys_list) {
            bool match = true;
            for (auto j = 0; j < num_input_fields; ++j) {
                if (!equal(keys[j], match_keys[j])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }

    bool is_null = false;
    bool is_length_inconsistent = false;
    int num_fields = 0;
    std::vector<std::vector<DatumKey>> keys_list = {};
};

struct MultiInStateFragmentLocal {
    MatchLists match_lists;
    ScalarFunction function;
};

} // namespace

Status CelonisMultiIn::prepare(starrocks::FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    auto state = new MultiInStateFragmentLocal();
    context->set_function_state(scope, state);
    auto match_column = context->get_constant_column(1);
    if (match_column == nullptr) {
        return Status::InvalidArgument("CELONIS_MULTI_IN: the second argument (struct_of_arrays) must be constant.");
    }
    state->function = multi_in_constant_config;
    if (match_column->empty()) {
        return Status::OK();
    }
    state->match_lists = MatchLists(match_column, 0);
    return Status::OK();
}

Status CelonisMultiIn::close(starrocks::FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const MultiInStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisMultiIn::multi_in([[maybe_unused]] starrocks::FunctionContext* context,
                                             const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    DCHECK(columns[0]->is_struct());
    const auto* state = reinterpret_cast<const MultiInStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

StatusOr<ColumnPtr> CelonisMultiIn::multi_in_constant_config([[maybe_unused]] starrocks::FunctionContext* context,
                                                             const starrocks::Columns& columns) {
    auto& input_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    const auto* state = reinterpret_cast<const MultiInStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || state->match_lists.is_null) {
            result.append_null();
            continue;
        }
        result.append(state->match_lists.match(input_fields, row));
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisMultiIn::multi_in_non_constant_config([[maybe_unused]] starrocks::FunctionContext* context,
                                                                 const starrocks::Columns& columns) {
    auto& input_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto& match_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[1].get()))->fields();
    const auto n_fields = input_fields.size();
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        if (match_fields.size() != n_fields) {
            result.append(false);
            continue;
        }
        std::optional<size_t> n_tuples;
        bool tuple_len_mismatch = false;
        for (auto j = 0; j < n_fields; ++j) {
            auto size = match_fields[j]->get(row).get_array().size();
            if (!n_tuples.has_value()) {
                n_tuples = size;
            } else {
                if (n_tuples.value() != size) {
                    tuple_len_mismatch = true;
                    break;
                }
            }
        }
        if (tuple_len_mismatch || !n_tuples.has_value()) {
            result.append(false);
            continue;
        }
        std::vector<DatumKey> keys;
        for (auto j = 0; j < n_fields; ++j) {
            keys.push_back(input_fields[j]->get(row).convert2DatumKey());
        }
        // traverse each tuple
        bool found_match = false;
        for (auto i = 0; i < n_tuples.value(); ++i) {
            bool match = true;
            // check each field
            for (auto j = 0; j < n_fields; ++j) {
                auto key = match_fields[j]->get(row).get_array()[i].convert2DatumKey();
                if (!equal(keys[j], key)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                found_match = true;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(all_const);
}

} // namespace starrocks
