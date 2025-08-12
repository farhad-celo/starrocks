#include "exprs/celonis/like.h"

#include <fmt/format.h>
#include <re2/re2.h>

#include <utility>

#include "column/binary_column.h"
#include "column/column_hash.h"
#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/like_predicate.h"
#include "util/utf8.h"

namespace starrocks {

static const RE2 HAS_WILDCARD(R"((.*([^\\]))?(\\\\)*[%_].*)", re2::RE2::Quiet);

struct LikeStateFragmentLocal {
    FunctionContext* translate_context = nullptr;
    std::string pattern_str;
};

struct LikeStateThreadLocal {
    FunctionContext* like_predicate_context = nullptr;
};

Status CelonisLike::like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::NotSupported("pattern supports const value only");
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::NotSupported("pattern does not support null");
        }

        auto pattern_column = context->get_constant_column(1);
        auto pattern = ColumnHelper::get_const_value<TYPE_VARCHAR>(pattern_column);
        std::string pattern_str = pattern.to_string();

        if (RE2::FullMatch(pattern_str, HAS_WILDCARD)) {
            // If there are wildcards in the pattern, LIKE will be called.
            // This implementation depends on the implementation detail of LikePredicate::like_prepare() which sets function state in THREAD_LOCAL only.
            return Status::OK();
        }

        auto type_desc_varchar = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        auto translate_return_type = type_desc_varchar;
        std::vector<FunctionContext::TypeDesc> translate_arg_types = {type_desc_varchar, type_desc_varchar,
                                                                      type_desc_varchar};
        auto translate_context = FunctionContext::create_context(context->state(), context->mem_pool(),
                                                                 translate_return_type, translate_arg_types);

        Columns translate_columns;
        translate_columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("%" + pattern_str + "%", 1));
        translate_columns.push_back(
                ColumnHelper::create_const_column<TYPE_VARCHAR>("abcdefghijklmnopqrstuvwxyzäöü", 1));
        translate_columns.push_back(
                ColumnHelper::create_const_column<TYPE_VARCHAR>("ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ", 1));
        translate_context->set_constant_columns(translate_columns);

        auto state = new LikeStateFragmentLocal();
        context->set_function_state(scope, state);
        state->translate_context = translate_context;

        // This implementation depends on the implementation detail of CelonisStringFunctions::translate_prepare() which sets function state in FRAGMENT_LOCAL only.
        auto status = CelonisStringFunctions::translate_prepare(translate_context, scope);
        if (!status.ok()) {
            return Status::InternalError(fmt::format("Failed to translate pattern. {}", status.message()));
        }
        ASSIGN_OR_RETURN(ColumnPtr pattern_upper_case_column,
                         CelonisStringFunctions::translate(translate_context, translate_columns));

        auto pattern_upper_case = ColumnHelper::get_const_value<TYPE_VARCHAR>(pattern_upper_case_column);
        state->pattern_str = pattern_upper_case.to_string();

        return Status::OK();
    }

    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (like_state_fragment_local == nullptr) {
        return LikePredicate::like_prepare(context, scope);
    }

    auto like_predicate_context = context->clone(context->mem_pool());
    like_predicate_context->set_function_state(FunctionContext::FRAGMENT_LOCAL, nullptr);

    auto state = new LikeStateThreadLocal();
    state->like_predicate_context = like_predicate_context;
    context->set_function_state(FunctionContext::THREAD_LOCAL, state);

    Columns like_columns;
    like_columns.push_back(context->get_constant_column(0));
    like_columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(like_state_fragment_local->pattern_str, 1));
    like_predicate_context->set_constant_columns(like_columns);
    return LikePredicate::like_prepare(like_predicate_context, scope);
}

Status CelonisLike::like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (like_state_fragment_local == nullptr) {
        return LikePredicate::like_close(context, scope);
    }
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        RETURN_IF_ERROR(CelonisStringFunctions::translate_close(like_state_fragment_local->translate_context, scope));
        delete like_state_fragment_local->translate_context;
        delete like_state_fragment_local;
    }
    if (scope == FunctionContext::THREAD_LOCAL) {
        const auto* like_state_thread_local = reinterpret_cast<const LikeStateThreadLocal*>(
                context->get_function_state(FunctionContext::THREAD_LOCAL));
        RETURN_IF_ERROR(LikePredicate::like_close(like_state_thread_local->like_predicate_context, scope));
        delete like_state_thread_local->like_predicate_context;
        delete like_state_thread_local;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisLike::like(FunctionContext* context, const Columns& columns) {
    const auto* like_state_fragment_local = reinterpret_cast<const LikeStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    if (like_state_fragment_local == nullptr) {
        return LikePredicate::like(context, columns);
    }
    const auto* like_state_thread_local =
            reinterpret_cast<const LikeStateThreadLocal*>(context->get_function_state(FunctionContext::THREAD_LOCAL));
    auto translate_context = like_state_fragment_local->translate_context;
    // TODO: add a case-insensitive search to Volnitsky.h and use it instead of translating input before search.
    // Since Volnitsky.h was initially forked, the origin has added VolnitskyCaseInsensitiveUTF8.
    // As an interim solution if necessary, we may add a dedicated translator optimized for German upper/lower, for example,
    // scanning all rows at once and handling a to z as a range as StringCaseToggleFunction in expr/string_functions.cpp.
    Columns translate_columns;
    translate_columns.push_back(columns[0]);
    translate_columns.push_back(translate_context->get_constant_column(1));
    translate_columns.push_back(translate_context->get_constant_column(2));
    ASSIGN_OR_RETURN(ColumnPtr input_upper_case_column,
                     CelonisStringFunctions::translate(translate_context, translate_columns));

    auto like_predicate_context = like_state_thread_local->like_predicate_context;
    Columns like_columns;
    like_columns.push_back(input_upper_case_column);
    like_columns.push_back(like_predicate_context->get_constant_column(1));
    like_predicate_context->set_constant_columns(like_columns);
    return LikePredicate::like(like_predicate_context, like_columns);
}

} // namespace starrocks
