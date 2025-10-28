#include "in_like.h"

#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/like/like.h"
#include "exprs/celonis/util.h"

namespace starrocks::celonis::like {

namespace v1 {
bool contains_wildcard(std::string_view pattern) {
    int backslash_count = 0;
    for (char ch : pattern) {
        if (ch == '%' || ch == '_') {
            // If the number of preceding backslashes is even (including 0), the wildcard is not escaped
            if (backslash_count % 2 == 0) {
                return true;
            }
        }
        if (ch == '\\') {
            ++backslash_count;
        } else {
            backslash_count = 0;
        }
    }
    return false;
}

bool match_helper(const std::string& input, const std::string& pattern, int i, int j) {
    if (j == pattern.length()) {    // End of pattern
        return i == input.length(); // True if also end of input
    }

    // Handling escaped wildcards
    if (pattern[j] == '\\' && j + 1 < pattern.length() &&
        (pattern[j + 1] == '%' || pattern[j + 1] == '_' || pattern[j + 1] == '\\')) {
        if (i < input.length() && input[i] == pattern[j + 1]) {
            return match_helper(input, pattern, i + 1, j + 2);
        }
        return false;
    }

    if (pattern[j] == '%') {
        for (int k = i; k <= input.length(); ++k) {
            if (match_helper(input, pattern, k, j + 1)) {
                return true;
            }
        }
    } else if (pattern[j] == '_') {
        if (i < input.length()) {
            return match_helper(input, pattern, i + 1, j + 1);
        }
    } else {
        if (i < input.length() && input[i] == pattern[j]) {
            return match_helper(input, pattern, i + 1, j + 1);
        }
    }
    return false;
}

std::string remove_escape(const std::string& str) {
    std::string rv;
    int backslash_count = 0;
    for (char c : str) {
        if (c == '\\') {
            ++backslash_count;
        } else {
            rv.append(backslash_count / 2, '\\');
            if (c != '_' && c != '%' && backslash_count % 2 == 1) {
                rv += '\\';
            }
            rv += c;
            backslash_count = 0;
        }
    }
    rv.append(backslash_count / 2 + backslash_count % 2, '\\');
    return rv;
}

static bool string_match(const std::string& input, const std::string& pattern) {
    const bool has_wildcard = contains_wildcard(pattern);
    if (!has_wildcard) {
        return lower_string_utf8(input).find(lower_string_utf8(remove_escape(pattern))) != std::string::npos;
    } else {
        return match_helper(input, pattern, 0, 0);
    }
}

StatusOr<ColumnPtr> in_like_non_constant_patterns(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    RETURN_IF_COLUMNS_ONLY_NULL({columns[1]});
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    // Handle constant patterns column here. SR may send a const patterns column chunk to this function.
    ColumnPtr patterns_column = ColumnHelper::unpack_and_duplicate_const_column(columns[1]->size(), columns[1]);
    UnnestedArrayData pattern_data = prepare_array_input(patterns_column.get());
    const auto& patterns = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(*pattern_data.elements).get_data().data();
    const auto& offsets = pattern_data.offsets->get_data().data();
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        // patterns is NULL
        if (columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        if (input_string_viewer.is_null(row)) {
            int64_t found_null = 0L;
            for (auto i = start; i < end; ++i) {
                if (pattern_data.null_elements != nullptr && (*pattern_data.null_elements)[i] != 0) {
                    found_null = 1L;
                    break;
                }
            }
            result.append(found_null);
            continue;
        }
        const std::string input_string = input_string_viewer.value(row).to_string();
        int64_t found_match = 0L;
        HashSet<std::string> pattern_seen;
        for (auto i = start; i < end; ++i) {
            if (pattern_data.null_elements != nullptr && (*pattern_data.null_elements)[i] != 0) {
                continue;
            }
            const std::string pattern = patterns[i].to_string();
            bool seen = !pattern_seen.insert(pattern).second;
            if (seen) {
                continue;
            }
            // TODO(o.layer): Could migrate to celonis::input_row_pattern_matcher
            if (string_match(input_string, pattern)) {
                found_match = 1L;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(all_const);
}

struct CelonisInLikeState {
    CelonisInLikeState() {}

    std::vector<std::string> patterns;
    std::vector<bool> case_insensitives;
    bool has_null = false;
    bool is_null = false;
    ScalarFunction function;
};

StatusOr<ColumnPtr> in_like_constant_patterns([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const auto* state =
            reinterpret_cast<const CelonisInLikeState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        if (state->is_null) {
            result.append_null();
            continue;
        }
        if (input_string_viewer.is_null(row)) {
            result.append(state->has_null ? 1L : 0L);
            continue;
        }
        const std::string input_string = input_string_viewer.value(row).to_string();
        std::optional<std::string> lower_input_string = std::nullopt;
        int64_t found_match = 0L;
        for (auto j = 0; j < state->patterns.size(); ++j) {
            bool matched = false;
            if (state->case_insensitives[j]) {
                if (!lower_input_string.has_value()) {
                    lower_input_string = lower_string_utf8(input_string);
                }
                matched = lower_input_string.value().find(state->patterns[j]) != std::string::npos;
            } else {
                matched = match_helper(input_string, state->patterns[j], 0, 0);
            }
            if (matched) {
                found_match = 1L;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(all_const);
}

Status CelonisInLike::in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new CelonisInLikeState();
    context->set_function_state(scope, state);

    auto pattern_column = context->get_constant_column(1);
    if (pattern_column == nullptr) {
        state->function = in_like_non_constant_patterns;
        return Status::OK();
    }
    state->function = in_like_constant_patterns;
    if (pattern_column->empty()) {
        return Status::OK();
    }
    if (pattern_column->is_null(0)) {
        state->is_null = true;
        return Status::OK();
    }
    auto pattern_array = pattern_column->get(0).get_array();
    HashSet<std::string> pattern_seen;
    for (const auto& pattern_datum : pattern_array) {
        if (pattern_datum.is_null()) {
            state->has_null = true;
            continue;
        }
        const std::string raw_pattern = pattern_datum.get_slice().to_string();
        bool seen = !pattern_seen.insert(raw_pattern).second;
        if (seen) {
            continue;
        }
        const bool has_wildcard = celonis::like::v1::contains_wildcard(raw_pattern);
        bool case_insensitive = !has_wildcard;
        const std::string modified_pattern =
                has_wildcard ? raw_pattern
                             : celonis::like::lower_string_utf8(celonis::like::v1::remove_escape(raw_pattern));
        state->patterns.push_back(modified_pattern);
        state->case_insensitives.push_back(case_insensitive);
    }
    return Status::OK();
}

Status CelonisInLike::in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<CelonisInLikeState*>(context->get_function_state(scope));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisInLike::in_like([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    const auto* state =
            reinterpret_cast<const CelonisInLikeState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}
} // namespace v1

namespace v2 {
struct CelonisInLikeState {
    CelonisInLikeState() {}

    std::vector<like_reusable> patterns;

    bool has_null = false;
    bool is_null = false;
    ScalarFunction function;
};

StatusOr<ColumnPtr> in_like_constant_patterns([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const auto* state =
            reinterpret_cast<const CelonisInLikeState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);

    for (size_t row = 0; row < num_rows; ++row) {
        if (state->is_null) {
            result.append_null();
            continue;
        }
        if (input_string_viewer.is_null(row)) {
            result.append(state->has_null ? 1L : 0L);
            continue;
        }

        int64_t found_match = 0L;
        std::string_view input = input_string_viewer.value(row);
        input_row_pattern_matcher row_pattern_matcher(input);
        for (const auto& pattern : state->patterns) {
            if (row_pattern_matcher.match(pattern)) {
                found_match = 1L;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(all_const);
}

Status CelonisInLike::in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new CelonisInLikeState();
    context->set_function_state(scope, state);

    auto pattern_column = context->get_constant_column(1);
    if (pattern_column == nullptr) {
        // Early exit for non-constant patterns.
        state->function = v1::in_like_non_constant_patterns;
        return Status::OK();
    }
    state->function = in_like_constant_patterns;

    if (pattern_column->empty()) {
        return Status::OK();
    }

    if (pattern_column->is_null(0)) {
        state->is_null = true;
        return Status::OK();
    }

    auto pattern_array = pattern_column->get(0).get_array();
    const auto num_patterns = pattern_array.size();

    // Deduplicate patterns.
    HashSet<std::string_view> pattern_seen;
    pattern_seen.reserve(num_patterns);
    for (const auto& pattern_datum : pattern_array) {
        if (pattern_datum.is_null()) {
            state->has_null = true;
            continue;
        }

        pattern_seen.insert(pattern_datum.get_slice());
    }

    state->function = in_like_constant_patterns;
    state->patterns.reserve(num_patterns);
    for (const auto& pattern : pattern_seen) {
        state->patterns.emplace_back(pattern_t{pattern});
    }

    return Status::OK();
}

Status CelonisInLike::in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<CelonisInLikeState*>(context->get_function_state(scope));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisInLike::in_like([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    const auto* state =
            reinterpret_cast<const CelonisInLikeState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}
} // namespace v2
} // namespace starrocks::celonis::like