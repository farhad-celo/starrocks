#include "trim.h"

#include <exprs/builtin_functions.h>
#include <util/hash.h>

#include <boost/algorithm/string/trim.hpp>

#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "column/datum_convert.h"

namespace starrocks {

namespace {

enum class TrimDirection { LEFT, RIGHT };

struct TrimStateFragmentLocal {
    ScalarFunction function;
    std::optional<phmap::flat_hash_set<char, StdHash<char>>> characters_opt{std::nullopt};
};

template <TrimDirection TRIM_DIRECTION>
void trim_if(std::string& value, const phmap::flat_hash_set<char, StdHash<char>>& characters) {
    if constexpr (TRIM_DIRECTION == TrimDirection::LEFT) {
        boost::trim_left_if(value, [&characters](char value_char) {
            return std::ranges::any_of(characters,
                                       [value_char](const char trim_char) { return value_char == trim_char; });
        });
    } else {
        boost::trim_right_if(value, [&characters](char value_char) {
            return std::ranges::any_of(characters,
                                       [value_char](const char trim_char) { return value_char == trim_char; });
        });
    }
}

template <typename TrimFn>
    requires std::invocable<TrimFn, std::string&, int32_t>
[[nodiscard]] ColumnPtr trim_for_each(
        const ColumnViewer<TYPE_VARCHAR>& input_column, TrimFn&& trim_fn,
        const std::optional<ColumnViewer<TYPE_VARCHAR>>& characters_column = std::nullopt) {
    const auto num_rows{static_cast<int32_t>(input_column.size())};
    ColumnBuilder<TYPE_VARCHAR> result_column{num_rows};

    for (auto row{0}; row < num_rows; ++row) {
        if (input_column.is_null(row) || (characters_column.has_value() && characters_column->is_null(row))) {
            result_column.append_null();
            continue;
        }
        auto value{input_column.value(row).to_string()};
        trim_fn(value, row);
        result_column.append(std::move(value));
    }

    return result_column.build(false);
}

template <TrimDirection TRIM_DIRECTION>
[[nodiscard]] StatusOr<ColumnPtr> trim_non_constant(FunctionContext* /* context */, const Columns& columns) {
    const auto input_column_viewer{ColumnViewer<TYPE_VARCHAR>(columns[0])};
    const auto characters_column_viewer{ColumnViewer<TYPE_VARCHAR>(columns[1])};
    phmap::flat_hash_set<char, StdHash<char>> unique_characters{};

    DCHECK_EQ(input_column_viewer.size(), characters_column_viewer.size());

    return trim_for_each(
            input_column_viewer,
            [&unique_characters, &characters_column_viewer](std::string& value, const auto row) {
                const auto characters{characters_column_viewer.value(row).to_string()};
                unique_characters.insert(characters.begin(), characters.end());
                trim_if<TRIM_DIRECTION>(value, unique_characters);
                unique_characters.clear();
            },
            characters_column_viewer);
}

template <TrimDirection TRIM_DIRECTION>
[[nodiscard]] StatusOr<ColumnPtr> trim_constant(FunctionContext* context, const Columns& columns) {
    const auto input_column_viewer{ColumnViewer<TYPE_VARCHAR>(columns[0])};
    const auto* state{reinterpret_cast<const TrimStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL))};
    const auto characters{state->characters_opt};
    if (!characters.has_value()) {
        return ColumnHelper::create_const_null_column(input_column_viewer.size());
    }

    return trim_for_each(input_column_viewer, [&characters](std::string& value, auto /* row */) {
        trim_if<TRIM_DIRECTION>(value, characters.value());
    });
}

StatusOr<ColumnPtr> trim(FunctionContext* context, const Columns& columns) {
    const auto* state{reinterpret_cast<const TrimStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL))};
    return state->function(context, columns);
}

template <TrimDirection TRIM_DIRECTION>
Status trim_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state{new TrimStateFragmentLocal()};
    context->set_function_state(scope, state);

    if (!context->is_constant_column(1)) {
        state->function = trim_non_constant<TRIM_DIRECTION>;
        return Status::OK();
    }

    if (const auto characters_column{context->get_constant_column(1)}; !characters_column->only_null()) {
        auto characters{ColumnHelper::get_const_value<TYPE_VARCHAR>(characters_column).to_string()};
        state->characters_opt = phmap::flat_hash_set<char, StdHash<char>>(characters.begin(), characters.end());
    }

    state->function = trim_constant<TRIM_DIRECTION>;

    return Status::OK();
}

} // namespace

StatusOr<ColumnPtr> CelonisTrim::ltrim(FunctionContext* context, const Columns& columns) {
    return trim(context, columns);
}

StatusOr<ColumnPtr> CelonisTrim::rtrim(FunctionContext* context, const Columns& columns) {
    return trim(context, columns);
}

Status CelonisTrim::ltrim_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return trim_prepare<TrimDirection::LEFT>(context, scope);
}

Status CelonisTrim::rtrim_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return trim_prepare<TrimDirection::RIGHT>(context, scope);
}

Status CelonisTrim::trim_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const TrimStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

} // namespace starrocks
