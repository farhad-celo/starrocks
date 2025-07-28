#include "exprs/celonis/like.h"

#include <fmt/format.h>
#include <re2/re2.h>

#include <utility>

#include "column/binary_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/anyval_util.h"
#include "exprs/like_predicate.h"
#include "gutil/strings/substitute.h"
#include "util/utf8.h"

namespace starrocks {

enum class LikeFunctionType {
    LIKE_NON_CONSTANT,
    LIKE_CONSTANT_NO_WILDCARD,
    LIKE_PREDICATE
};

struct LikeStateFragmentLocal {
    std::shared_ptr<re2::RE2> pattern_re2;
    ScalarFunction function;
    LikeFunctionType function_type;
};

Status CelonisLike::like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto state = new LikeStateFragmentLocal();
        context->set_function_state(scope, state);

        if (!context->is_constant_column(1)) {
            state->function = like_non_constant;
            state->function_type = LikeFunctionType::LIKE_NON_CONSTANT;
            return Status::OK();
        }

        auto pattern_column = context->get_constant_column(1);
        auto pattern = ColumnHelper::get_const_value<TYPE_VARCHAR>(pattern_column);
        auto re_pattern_result = convert_like_pattern(pattern);

        auto& re_pattern_str = re_pattern_result.first;
        auto& has_wildcard = re_pattern_result.second;

        if (has_wildcard) {
            // If there are wildcards in the pattern, LIKE will be called.
            // This implementation depends on the implementation detail of LikePredicate::like_prepare() which sets function state in THREAD_LOCAL only.
            state->function = LikePredicate::like;
            state->function_type = LikeFunctionType::LIKE_PREDICATE;
            return Status::OK();
        }

        RE2::Options opts;
        opts.set_never_nl(false);
        opts.set_dot_nl(true);
        opts.set_log_errors(false);
        opts.set_case_sensitive(false);

        state->pattern_re2 = std::make_shared<re2::RE2>(re_pattern_str, opts);
        state->function = like_constant_no_wildcard;
        state->function_type = LikeFunctionType::LIKE_CONSTANT_NO_WILDCARD;

        return Status::OK();
    }

    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (like_state_fragment_local->function_type == LikeFunctionType::LIKE_PREDICATE) {
        return LikePredicate::like_prepare(context, scope);
    }

    return Status::OK();
}

Status CelonisLike::like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        delete like_state_fragment_local;
    } else if (like_state_fragment_local->function_type == LikeFunctionType::LIKE_PREDICATE) {
        return LikePredicate::like_close(context, scope);
    }
    return Status::OK();
}

// Borrowed from exprs/like_predicate.cpp.
std::pair<std::string, bool> CelonisLike::convert_like_pattern(const Slice& pattern) {
    std::string re_pattern;
    re_pattern.clear();

    bool is_escaped = false;
    bool has_wildcard = false;

    for (int i = 0; i < pattern.size; ++i) {
        if (!is_escaped && pattern.data[i] == '%') {
            re_pattern.append(".*");
            has_wildcard = true;
        } else if (!is_escaped && pattern.data[i] == '_') {
            re_pattern.append(".");
            has_wildcard = true;
            // check for escape char before checking for regex special chars, they might overlap
        } else if (!is_escaped && pattern.data[i] == '\\') {
            is_escaped = true;
        } else if (pattern.data[i] == '.' || pattern.data[i] == '[' || pattern.data[i] == ']' ||
                   pattern.data[i] == '{' || pattern.data[i] == '}' || pattern.data[i] == '(' ||
                   pattern.data[i] == ')' || pattern.data[i] == '\\' || pattern.data[i] == '*' ||
                   pattern.data[i] == '+' || pattern.data[i] == '?' || pattern.data[i] == '|' ||
                   pattern.data[i] == '^' || pattern.data[i] == '$') {
            // escape all regex special characters; see list at
            re_pattern.append("\\");
            re_pattern.append(1, pattern.data[i]);
            is_escaped = false;
        } else {
            // regular character or escaped special character
            re_pattern.append(1, pattern.data[i]);
            is_escaped = false;
        }
    }

    return std::make_pair(re_pattern, has_wildcard);
}

StatusOr<ColumnPtr> CelonisLike::like_non_constant(FunctionContext* context, const Columns& columns) {
    const auto& value_column = VECTORIZED_FN_ARGS(0);
    const auto& pattern_column = VECTORIZED_FN_ARGS(1);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    ColumnViewer<TYPE_VARCHAR> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);
    ColumnViewer<TYPE_VARCHAR> pattern_viewer(pattern_column);

    RE2::Options opts;
    opts.set_never_nl(false);
    opts.set_dot_nl(true);
    opts.set_log_errors(false);

    for (int row = 0; row < num_rows; ++row) {
        if (value_viewer.is_null(row) || pattern_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        auto re_pattern_result = convert_like_pattern(pattern_viewer.value(row));
        auto& re_pattern = re_pattern_result.first;
        auto& has_wildcard = re_pattern_result.second;

        opts.set_case_sensitive(has_wildcard);
        re2::RE2 re(re_pattern, opts);

        if (!re.ok()) {
            context->set_error(strings::Substitute("Invalid regex: $0", re_pattern).c_str());
            result.append_null();
            continue;
        }

        if (has_wildcard) {
            auto v = RE2::FullMatch(re2::StringPiece(value_viewer.value(row).data, value_viewer.value(row).size), re);
            result.append(v);
        } else {
            auto v =
                    RE2::PartialMatch(re2::StringPiece(value_viewer.value(row).data, value_viewer.value(row).size), re);
            result.append(v);
        }
    }

    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisLike::like_constant_no_wildcard(FunctionContext* context, const Columns& columns) {
    const auto& value_column = VECTORIZED_FN_ARGS(0);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    const auto& pattern_re2 = *like_state_fragment_local->pattern_re2;
    if (!pattern_re2.ok()) {
        context->set_error(strings::Substitute("Invalid regex: $0", pattern_re2.error()).c_str());
        return ColumnHelper::create_const_null_column(num_rows);
    }

    ColumnViewer<TYPE_VARCHAR> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    for (int row = 0; row < num_rows; ++row) {
        if (value_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto v = RE2::PartialMatch(re2::StringPiece(value_viewer.value(row).data, value_viewer.value(row).size),
                                   pattern_re2);
        result.append(v);
    }

    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisLike::like(FunctionContext* context, const Columns& columns) {
    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return like_state_fragment_local->function(context, columns);
}

} // namespace starrocks
