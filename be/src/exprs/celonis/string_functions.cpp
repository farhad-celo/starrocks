#include "exprs/celonis/string_functions.h"

#include <boost/locale/utf.hpp>
#include <utility>

#include "column/binary_column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "util/phmap/phmap.h"
#include "util/utf8.h"

namespace starrocks {

struct CelonisTranslateState {
    CelonisTranslateState(Slice pattern, Slice replace)
            : pattern_chars(pattern.to_string()), replace_chars(replace.to_string()) {}
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
    Slice pattern = ColumnHelper::get_const_value<TYPE_VARCHAR>(pattern_col);
    if (pattern.empty()) {
        return Status::InvalidArgument("The second parameter should not be empty string");
    }

    const auto replace_col = context->get_constant_column(2);
    Slice replace = ColumnHelper::get_const_value<TYPE_VARCHAR>(replace_col);
    if (replace.empty()) {
        return Status::InvalidArgument("The third parameter should not be empty string");
    }

    auto *state = new CelonisTranslateState(pattern, replace);
    context->set_function_state(scope, state);

    Slice pattern_chars{state->pattern_chars};
    Slice replace_chars{state->replace_chars};

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
        auto* state = reinterpret_cast<CelonisTranslateState*>(context->get_function_state(scope));
        delete state;
    }

    return Status::OK();
}

StatusOr<ColumnPtr> CelonisStringFunctions::translate(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const CelonisTranslateState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
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

    StatusOr<ColumnPtr> CelonisStringFunctions::sanitize_invalid_utf8(starrocks::FunctionContext *context,
                                                                      const starrocks::Columns &columns) {
    auto str_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);

    auto size = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> result(size);
    for (int row = 0; row < size; ++row) {
        if (str_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        // Sanitization logic is copied from query-engine/src/main/native/cpm-accelerator/modules/format/src/utf/utf_utils.cpp
        // in cpm-query-engine repository.
        auto input = std::string_view(str_viewer.value(row));
        std::string sanitized;
        sanitized.reserve(input.length());

        constexpr char REPLACEMENT_CHAR{'?'};

        for (const auto* itr{input.begin()}; itr != input.end();) {
            const auto decoded{boost::locale::utf::utf_traits<char>::decode(itr, input.end())};
            if (decoded == boost::locale::utf::illegal || decoded == boost::locale::utf::incomplete) {
                sanitized.push_back(REPLACEMENT_CHAR);
            } else {
                boost::locale::utf::utf_traits<char>::encode(decoded, std::back_inserter(sanitized));
            }
        }

        result.append(Slice(sanitized));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
