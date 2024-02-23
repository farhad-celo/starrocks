#include "exprs/celonis/string_functions.h"

#include <boost/locale/utf.hpp>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "column/binary_column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "util/phmap/phmap.h"
#include "util/utf8.h"
#include "exprs/celonis/util.h"

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

    auto* state = new CelonisTranslateState(pattern, replace);
    context->set_function_state(scope, state);

    Slice pattern_chars{state->pattern_chars};
    Slice replace_chars{state->replace_chars};

    const char* replace_p = replace_chars.get_data();
    const char* replace_end = replace_p + replace_chars.get_size();

    const char* pattern_p = pattern_chars.get_data();
    const char* pattern_end = pattern_p + pattern_chars.get_size();

    for (int replace_char_size = 0, pattern_char_size = 0; replace_p < replace_end && pattern_p <
                                                                                      pattern_end; replace_p += replace_char_size, pattern_p += pattern_char_size) {
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
    const auto& translate_mapping = state->translate_mapping;

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
        for (const char* str_p = str_value.get_data(), * str_end = str_p + str_value.get_size();
             str_p < str_end; str_p += char_size) {
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

StatusOr<ColumnPtr> CelonisStringFunctions::sanitize_invalid_utf8(starrocks::FunctionContext* context,
                                                                  const starrocks::Columns& columns) {
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
        size_t found = input.find('\0');
        input = input.substr(0, found);
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


static bool split_index(const Slice& haystack, const Slice& delimiter, int32_t part_number, Slice& res) {
    if (part_number >= 0) {
        part_number++;
        if (delimiter.size == 1) {
            // if delimiter is a char, use memchr to split
            // Record the two adjacent offsets when matching delimiter.
            // If no matching, return NULL.
            // Else return the string between two adjacent offsets.
            int32_t pre_offset = -1;
            int32_t offset = -1;
            int32_t num = 0;
            while (num < part_number) {
                pre_offset = offset;
                size_t n = haystack.size - offset - 1;
                char* pos = reinterpret_cast<char*>(memchr(haystack.data + offset + 1, delimiter.data[0], n));
                if (pos != nullptr) {
                    offset = pos - haystack.data;
                    num++;
                } else {
                    offset = haystack.size;
                    num = (num == 0) ? 0 : num + 1;
                    break;
                }
            }

            if (num == part_number) {
                res.data = haystack.data + pre_offset + 1;
                res.size = offset - pre_offset - 1;
                return true;
            }
        } else {
            // if delimiter is a string, use memmem to split
            int32_t pre_offset = -static_cast<int32_t>(delimiter.size);
            int32_t offset = -static_cast<int32_t>(delimiter.size);
            int32_t num = 0;
            while (num < part_number) {
                pre_offset = offset;
                size_t n = haystack.size - offset - delimiter.size;
                char* pos = reinterpret_cast<char*>(
                        memmem(haystack.data + offset + delimiter.size, n, delimiter.data, delimiter.size));
                if (pos != nullptr) {
                    offset = pos - haystack.data;
                    num++;
                } else {
                    offset = haystack.size;
                    num = (num == 0) ? 0 : num + 1;
                    break;
                }
            }

            if (num == part_number) {
                res.data = haystack.data + pre_offset + delimiter.size;
                res.size = offset - pre_offset - delimiter.size;
                return true;
            }
        }
    } else {
        part_number = -part_number;
        auto haystack_str = haystack.to_string();
        int32_t offset = haystack.size;
        int32_t pre_offset = offset;
        int32_t num = 1;
        auto substr = haystack_str;
        while (num <= part_number && offset >= 0) {
            offset = (int) substr.rfind(delimiter, offset);
            if (offset != -1) {
                if (num == part_number) {
                    break;
                }
                pre_offset = offset;
                offset = offset - 1;
                substr = haystack_str.substr(0, pre_offset);
                num++;
            } else {
                break;
            }
        }
        if (num == part_number) {
            if (offset == -1) {
                res.data = haystack.data;
                res.size = pre_offset;
            } else {
                res.data = haystack.data + offset + delimiter.size;
                res.size = pre_offset - offset - delimiter.size;
            }
            return true;
        }
    }
    return false;
}

/**
 * @param: [haystack, delimiter, part_number]
 * @paramType: [BinaryColumn, BinaryColumn, IntColumn]
 * @return: BinaryColumn
 */
// The implementation is based on StringFunctions::split_part() and modified to match PQL behaviors.
StatusOr<ColumnPtr> CelonisStringFunctions::string_split(FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    ColumnViewer haystack_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    ColumnViewer delimiter_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer part_number_viewer = ColumnViewer<TYPE_INT>(columns[2]);

    size_t size = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> res(size);
    Slice slice;
    for (int i = 0; i < size; ++i) {
        if (haystack_viewer.is_null(i) || delimiter_viewer.is_null(i) || part_number_viewer.is_null(i)) {
            res.append_null();
            continue;
        }

        int32_t part_number = part_number_viewer.value(i);
        Slice haystack = haystack_viewer.value(i);
        Slice delimiter = delimiter_viewer.value(i);
        if (delimiter.size == 0) {
            // Keep Consistent with split.
            if (haystack.size == 0 && (part_number == 0 || part_number == 1)) {
                res.append(haystack);
            } else if (part_number >= 0) {
                if (part_number >= haystack.size) {
                    res.append_null();
                } else {
                    int char_size = 0, h = 0;
                    for (auto num = 0; h < haystack.size && num < part_number; h += char_size) {
                        char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<unsigned char>(haystack.data[h])];
                        ++num;
                    }
                    if (h >= haystack.size) {
                        if (part_number == 0) {
                            res.append(haystack);
                        } else {
                            res.append_null();
                        }
                    } else {
                        char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<unsigned char>(haystack.data[h])];
                        res.append(Slice(haystack.data + h, char_size));
                    }
                }
            } else {
                part_number = -part_number;
                std::vector<int> utf8_char_offsets;
                int char_size = 0;
                for (int h = 0; h < haystack.size; h += char_size) {
                    utf8_char_offsets.push_back(h);
                    char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<unsigned char>(haystack.data[h])];
                }
                if (part_number > utf8_char_offsets.size()) {
                    res.append_null();
                } else {
                    auto offset = utf8_char_offsets[utf8_char_offsets.size() - part_number];
                    char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<unsigned char>(haystack.data[offset])];
                    res.append(Slice(haystack.data + offset, char_size));
                }
            }
        } else {
            if (split_index(haystack, delimiter, part_number, slice)) {
                res.append(slice);
            } else if (part_number == 0) {
                res.append(haystack);
            } else {
                res.append_null();
            }
        }
    }
    return res.build(ColumnHelper::is_all_const(columns));
}

// Trims leading and trailing spaces from a string
static std::string trim(const std::string& input) {
    std::string result = input;

    // Left trim
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));

    // Right trim
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), result.end());

    return result;
}

struct comma_separator_facet : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }

    std::string do_grouping() const override { return "\3"; }
};

static const std::locale& get_locale() {
    static std::optional<std::locale> locale;
    static std::once_flag once_flag;

    // Thread-safe lazy initialization of a single locale instance which is used multiple times
    std::call_once(once_flag, []() { locale = std::locale("en_US.UTF-8"); });
    return *locale;
}


static std::optional<double> to_double(const std::string& input_string) {
    // std::stringstream is about 2.5x faster than atof on large inputs
    std::stringstream ss{};
    const std::locale& en_us_utf8_locale = get_locale();
    std::locale loc_with_thousands_sep{en_us_utf8_locale, new comma_separator_facet};
    // Set the numeric locale to "en_US.UTF-8" for proper parsing
    ss.imbue(loc_with_thousands_sep);

    std::string trimmed_input_string = trim(input_string);
    // Leading whitespaces should already be trimmed.
    ss << std::noskipws << trimmed_input_string;

    double result;
    // Attempt to convert the input string to a double
    ss >> result;

    // Check if the conversion was successful and the entire input was consumed
    if (ss.eof() && !ss.fail()) {
        if (std::isnan(result) || std::isinf(result)) {
            // Conversion result is NaN or infinity, return nullopt;
            return std::nullopt;
        }
        return result;
    } else {
        return std::nullopt;
    }
}

std::optional<int64_t> to_int64(const std::string& str) {
    try {
        int64_t value = std::stoll(trim(str));
        return value;
    } catch (const std::exception&) {
        // catch std::invalid_argument, std::out_of_range, and other std::exception-based exceptions
        return std::nullopt;
    }
}

StatusOr<ColumnPtr>
CelonisStringFunctions::string_to_int([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    size_t size = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> res(size);
    for (int i = 0; i < size; ++i) {
        if (input_string_viewer.is_null(i)) {
            res.append_null();
            continue;
        }
        std::string input_string = input_string_viewer.value(i).to_string();
        if (input_string.find_first_of("eE") != std::string::npos) {
            res.append_null();
            continue;
        }
        std::optional<int64_t> result = to_int64(input_string);
        if (result.has_value()) {
            res.append(result.value());
        } else {
            res.append_null();
        }
    }
    return res.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisStringFunctions::string_to_double(FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    size_t size = columns[0]->size();
    ColumnBuilder<TYPE_DOUBLE> res(size);
    for (int i = 0; i < size; ++i) {
        if (input_string_viewer.is_null(i)) {
            res.append_null();
            continue;
        }
        std::string input_string = input_string_viewer.value(i).to_string();
        std::optional<double> result = to_double(input_string);
        if (result.has_value()) {
            res.append(std::move(result.value()));
        } else {
            res.append_null();
        }

    }
    return res.build(ColumnHelper::is_all_const(columns));
}

static bool match_helper(const std::string& input, const std::string& pattern, int i, int j, bool case_insensitive) {
    if (j == pattern.length()) { // End of pattern
        return i == input.length(); // True if also end of input
    }

    // Handling escaped wildcards
    if (pattern[j] == '\\' && j + 1 < pattern.length() && (pattern[j + 1] == '%' || pattern[j + 1] == '_')) {
        if (i < input.length() && ((case_insensitive && tolower(input[i]) == tolower(pattern[j + 1])) ||
                                   (!case_insensitive && input[i] == pattern[j + 1]))) {
            return match_helper(input, pattern, i + 1, j + 2, case_insensitive);
        }
        return false;
    }

    if (pattern[j] == '%') {
        for (int k = i; k <= input.length(); ++k) {
            if (match_helper(input, pattern, k, j + 1, case_insensitive)) {
                return true;
            }
        }
    } else if (pattern[j] == '_') {
        if (i < input.length()) {
            return match_helper(input, pattern, i + 1, j + 1, case_insensitive);
        }
    } else {
        if (i < input.length() && ((case_insensitive && tolower(input[i]) == tolower(pattern[j])) ||
                                   (!case_insensitive && input[i] == pattern[j]))) {
            return match_helper(input, pattern, i + 1, j + 1, case_insensitive);
        }
    }
    return false;
}

static bool string_match(const std::string& input, const std::string& pattern) {
    bool has_wildcard = pattern.find_first_of("%_") != std::string::npos;
    bool case_insensitive = !has_wildcard;
    std::string modified_pattern = has_wildcard ? pattern : "%" + pattern + "%";
    return match_helper(input, modified_pattern, 0, 0, case_insensitive);
}

StatusOr<ColumnPtr>
CelonisStringFunctions::in_like([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    UnnestedArrayData pattern_data = prepare_array_input(columns[1].get());
    const auto& patterns = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(*pattern_data.elements).get_data().data();
    const auto& offsets = pattern_data.offsets->get_data().data();
    size_t n_rows = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
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
        for (auto i = start; i < end; ++i) {
            if (pattern_data.null_elements != nullptr && (*pattern_data.null_elements)[i] != 0) {
                continue;
            }
            const std::string pattern = patterns[i].to_string();
            if (string_match(input_string, pattern)) {
                found_match = 1L;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

static int edit_distance(const std::string& str1, const std::string& str2) {
    const int len1 = str1.size();
    const int len2 = str2.size();
    std::vector<std::vector<int>> dp(len1 + 1, std::vector<int>(len2 + 1));
    // Initialize the table with default values
    for (int i = 0; i <= len1; i++) {
        dp[i][0] = i;  // Deletion
    }
    for (int j = 0; j <= len2; j++) {
        dp[0][j] = j;  // Insertion
    }
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (str1[i - 1] == str2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1,    // Deletion
                                 dp[i][j - 1] + 1,    // Insertion
                                 dp[i - 1][j - 1] + cost}); // Substitution
        }
    }
    return dp[len1][len2];
}

StatusOr<ColumnPtr>
CelonisStringFunctions::match_strings([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 4);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    UnnestedArrayData match_string_data = prepare_array_input(columns[1].get());
    const auto& match_strings = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *match_string_data.elements).get_data().data();
    const auto& offsets = match_string_data.offsets->get_data().data();
    ColumnViewer top_k_viewer = ColumnViewer<TYPE_INT>(columns[2]);
    ColumnViewer separator_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    size_t n_rows = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string input_string = input_string_viewer.value(row).to_string();
        std::unordered_set<char> char_set(input_string.begin(), input_string.end());
        const std::string chars(char_set.begin(), char_set.end());
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        std::unordered_set<std::string> match_string_set;
        std::vector<std::pair<int, std::string>> pairs;
        for (auto i = start; i < end; ++i) {
            if (match_string_data.null_elements != nullptr && (*match_string_data.null_elements)[i] != 0) {
                continue;
            }
            const std::string match_string = match_strings[i].to_string();
            if (match_string.find_first_of(chars) != std::string::npos) {
                match_string_set.insert(match_string);
            }
        }
        for (const auto& match_string: match_string_set) {
            pairs.emplace_back(edit_distance(input_string, match_string), match_string);
        }
        std::sort(pairs.begin(), pairs.end());
        int top_k = columns[2]->is_null(row) ? 1 : top_k_viewer.value(row);
        const std::string separator = columns[3]->is_null(row) ? ", " : separator_viewer.value(row).to_string();
        std::string sep = "";
        std::string joined = "";
        for (const auto& p: pairs) {
            if (top_k-- > 0) {
                joined += sep;
                joined += p.second;
            }
            sep = separator;
        }
        result.append(joined);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
