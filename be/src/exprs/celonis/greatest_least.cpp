#include "exprs/celonis/greatest_least.h"

#include "column/column_builder.h"
#include "exprs/function_context.h"
#include "exprs/math_functions.h"
#include "types/logical_type.h"

namespace starrocks {

namespace {

enum class ComparisonType { GREATEST, LEAST };

template <typename>
inline constexpr bool always_false_v{false};

template <ComparisonType CMP_TYPE, LogicalType LT>
[[nodiscard]] ColumnPtr celonis_greatest_least_impl(FunctionContext* context, const Columns& columns) {
    if (columns.size() == 1) {
        return columns[0]->clone();
    }

    // If none of the columns contains a null value, we can simply defer the computation to the existing
    // Starrocks greatest/least implementation.
    if (const bool all_columns_without_null{std::none_of(
                columns.begin(), columns.end(), [](const ColumnPtr& column_ptr) { return column_ptr->has_null(); })};
        all_columns_without_null) {
        // At the time of this implementation, Starrocks' greatest/least implementation never returns a non-OK
        // status. Thus, we access the value of the returned StatusOr without further checks.
        if constexpr (CMP_TYPE == ComparisonType::GREATEST) {
            return MathFunctions::greatest<LT>(context, columns).value();
        } else if constexpr (CMP_TYPE == ComparisonType::LEAST) {
            return MathFunctions::least<LT>(context, columns).value();
        } else {
            static_assert(always_false_v<decltype(CMP_TYPE)>);
        }
    }

    // Else, at least one column contains a null value. Thus, we use the 'custom' Celonis null behaviour.
    // The following was copied and slightly adapted from the 'greatest' implementation in expr/math_functions.h
    std::vector<ColumnViewer<LT>> column_views{};
    column_views.reserve(columns.size());
    std::transform(columns.begin(), columns.end(), std::back_inserter(column_views), [](const ColumnPtr& value) {
        using ColumnViewerType = typename decltype(column_views)::value_type;
        return ColumnViewerType{value};
    });

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<LT> result{static_cast<int32_t>(num_rows)};
    for (std::size_t row_idx{0}; row_idx < num_rows; ++row_idx) {
        auto column_view_it{std::begin(column_views)};
        bool is_all_null{column_view_it->is_null(row_idx)};
        // N.B: If the value is null, value(...) returns a default value.
        auto result_column_value{column_view_it++->value(row_idx)};
        for (; column_view_it != std::cend(column_views); ++column_view_it) {
            const auto& column_view{*column_view_it};
            const bool is_null{column_view.is_null(row_idx)};
            if (!is_null) {
                const auto current_value{column_view.value(row_idx)};
                if (is_all_null) {
                    // In this branch, all previous values have been null and the current one is not. Thus, we
                    // simply assign the current value. Another option would be using a std::optional for
                    // 'result_column_value'. However, either way, we will have two more branches compared to
                    // the original Starrocks implementation: 1) We should only update 'result_column_value' if
                    // the current value is not null, and 2) we need a check if it is the first non-null value.
                    result_column_value = current_value;
                } else {
                    if constexpr (CMP_TYPE == ComparisonType::GREATEST) {
                        result_column_value = std::max(result_column_value, current_value);
                    } else if constexpr (CMP_TYPE == ComparisonType::LEAST) {
                        result_column_value = std::min(result_column_value, current_value);
                    } else {
                        static_assert(always_false_v<decltype(CMP_TYPE)>);
                    }
                }
            }
            is_all_null = is_all_null && is_null;
        }
        result.append(result_column_value, is_all_null);
    }

    return result.build(all_const);
}

template <ComparisonType CMP_TYPE>
[[nodiscard]] ColumnPtr celonis_greatest_least_impl(FunctionContext* context, const Columns& columns) {
    switch (const auto type{context->get_return_type().type}; type) {
    case TYPE_VARCHAR:
        return celonis_greatest_least_impl<CMP_TYPE, TYPE_VARCHAR>(context, columns);
    case TYPE_BIGINT:
        return celonis_greatest_least_impl<CMP_TYPE, TYPE_BIGINT>(context, columns);
    case TYPE_DOUBLE:
        return celonis_greatest_least_impl<CMP_TYPE, TYPE_DOUBLE>(context, columns);
    case TYPE_DATETIME:
        return celonis_greatest_least_impl<CMP_TYPE, TYPE_DATETIME>(context, columns);
    default: {
        // Should be prevented by the grammar and thus never be reached
        std::stringstream error_msg_strm{};
        error_msg_strm << "Column type '" << logical_type_to_string(type) << "' not supported.";
        throw std::runtime_error(error_msg_strm.str());
    }
    }
}

} // anonymous namespace

StatusOr<ColumnPtr> CelonisGreatestLeast::celonis_greatest(FunctionContext* context, const Columns& columns) {
    return celonis_greatest_least_impl<ComparisonType::GREATEST>(context, columns);
}

StatusOr<ColumnPtr> CelonisGreatestLeast::celonis_least(FunctionContext* context, const Columns& columns) {
    return celonis_greatest_least_impl<ComparisonType::LEAST>(context, columns);
}

} // namespace starrocks
