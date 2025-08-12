#include "exprs/celonis/string_functions.h"

#include <utility>

#include "column/binary_column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "util/phmap/phmap.h"
#include "util/utf8.h"

namespace starrocks {

struct TranslateState {
    std::string pattern_chars;
    std::string replace_chars;
    phmap::flat_hash_map<Slice, Slice, SliceHashWithSeed<PhmapSeed1>, SliceEqual> translate_mapping;
};

Status CelonisStringFunctions::translate_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    if (context->get_num_constant_columns() != 3) {
        return Status::InvalidArgument(
                "Translate needs three parameters: the column, the patterns, and the replacements");
    }

    if (!context->is_constant_column(1)) {
        return Status::InvalidArgument("The second parameter of trim only accept literal value");
    }
    if (!context->is_notnull_constant_column(1)) {
        return Status::InvalidArgument("The second parameter should not be null");
    }

    if (!context->is_constant_column(2)) {
        return Status::InvalidArgument("The third parameter of trim only accept literal value");
    }
    if (!context->is_notnull_constant_column(2)) {
        return Status::InvalidArgument("The third parameter should not be null");
    }
    const auto pattern_col = context->get_constant_column(1);
    Slice pattern_chars = ColumnHelper::get_const_value<TYPE_VARCHAR>(pattern_col);
    std::string pattern(pattern_chars.get_data(), pattern_chars.get_size());
    if (pattern.empty()) {
        return Status::InvalidArgument("The second parameter should not be empty string");
    }

    const auto replace_col = context->get_constant_column(2);
    Slice replace_chars = ColumnHelper::get_const_value<TYPE_VARCHAR>(replace_col);
    std::string replace(replace_chars.get_data(), replace_chars.get_size());
    if (replace.empty()) {
        return Status::InvalidArgument("The third parameter should not be empty string");
    }

    auto *state = new TranslateState();
    context->set_function_state(scope, state);

    state->pattern_chars = std::move(pattern);
    pattern_chars = Slice(state->pattern_chars);

    state->replace_chars = std::move(replace);
    replace_chars = Slice(state->replace_chars);

    const char *replace_p = replace_chars.get_data();
    const char *replace_end = replace_p + replace_chars.get_size();

    const char *pattern_p = pattern_chars.get_data();
    const char *pattern_end = pattern_p + pattern_chars.get_size();

    for (int replace_char_size = 0, pattern_char_size = 0; replace_p < replace_end && pattern_p < pattern_end; replace_p += replace_char_size, pattern_p += pattern_char_size) {
        replace_char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<uint8_t>(*replace_p)];
        pattern_char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<uint8_t>(*pattern_p)];

        state->translate_mapping.emplace(Slice(pattern_p, pattern_char_size), Slice(replace_p, replace_char_size));
    }

    if (replace_p != replace_end || pattern_p != pattern_end) {
        return Status::InvalidArgument("The second parameter does not have the same length as the third parameter");
    }

    return Status::OK();
}

Status CelonisStringFunctions::translate_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<TranslateState*>(context->get_function_state(scope));
        delete state;
    }

    return Status::OK();
}

StatusOr<ColumnPtr> CelonisStringFunctions::translate(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const TranslateState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    DCHECK(state != nullptr);
    const auto & translate_mapping = state->translate_mapping;

    auto str_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);

    auto size = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> result(size);
    for (int row = 0; row < size; ++row) {
        if (str_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        const auto str_value = str_viewer.value(row);
        std::string result_str;
        result_str.reserve(str_value.get_size());

        int char_size = 0;
        for (const char *str_p = str_value.get_data(), *str_end = str_p + str_value.get_size(); str_p < str_end; str_p += char_size) {
            char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<uint8_t>(*str_p)];

            const auto cit = translate_mapping.find(Slice(str_p, char_size));
            if (cit == translate_mapping.end()) {
                result_str.append(str_p, char_size);
            } else {
                result_str.append(cit->second);
            }
        }
        result.append(Slice(result_str.data(), result_str.size()));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
