#include "exprs/celonis/in_json.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "exprs/celonis/base64.h"
#include "exprs/celonis/util.h"
#include "nlohmann/json.hpp"

namespace starrocks {

namespace {

using json = nlohmann::json;

bool is_likely_base64_compressed(const std::string& str) {
    // If the string is empty, it's not base64
    if (str.empty()) {
        return false;
    }
    // Skip leading whitespace to find the first non-whitespace character
    size_t first_non_space = str.find_first_not_of(" \t\n\r");

    // If all characters are whitespace, it's not base64
    if (first_non_space == std::string::npos) {
        return false;
    }
    // Check if the first non-whitespace character indicates JSON
    char first_char = str[first_non_space];
    if (first_char == '[' || first_char == '{' || first_char == '"') {
        return false;
    }
    // If it's not JSON, assume it's base64 encoded compressed string
    return true;
}


// To use SliceHashSet for TYPE_VARCHAR. Copied from ../in_const_predicate.hpp.
template<LogicalType LT, typename Enable = void>
struct LHashSet {
    using LType = HashSet<RunTimeCppType<LT>>;
};

template<LogicalType LT>
struct LHashSet<LT, std::enable_if_t<isSliceLT<LT>>> {
    using LType = SliceHashSet;
};

template<LogicalType LT>
using LHashSetType = typename LHashSet<LT>::LType;

} // namespace

template<LogicalType LT>
struct InJsonStateFragmentLocal {
    LHashSetType<LT> match_set;
    bool match_has_null = false;
    ScalarFunction function;
    // only used when LT is TYPE_VARCHAR.
    std::vector<std::string> strings;
};

template<LogicalType LT>
Status CelonisInJson<LT>::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new InJsonStateFragmentLocal<LT>();
    context->set_function_state(scope, state);

    // Check if match_column is array type
    bool is_array = context->get_arg_type(1)->type == TYPE_ARRAY;
    std::string function_name = is_array ? "CELONIS_IN_JSON_ARRAY" : "CELONIS_IN_JSON";
    auto match_column = context->get_constant_column(1);
    if (match_column == nullptr) {
        state->function = is_array ? in_json_array_non_constant_match : in_json_non_constant_match;
        return Status::InvalidArgument("The non-const version of [" + function_name + "] should not be called.");
    }
    state->function = in_json_constant_match;

    if (match_column->is_null(0)) {
        return Status::OK();
    }

    std::string json_str_raw;

    if (is_array) {
        // Handle ARRAY_VARCHAR case
        auto array = match_column->get(0).get_array();
        size_t size = 0;
        for (const auto& element: array) {
            if (element.is_null()) {
                return Status::InvalidArgument("[" + function_name + "] Array can not contain null values.");
            } else {
                size += element.get_slice().size;
            }
        }
        json_str_raw.reserve(size);
        for (const auto& element: array) {
            json_str_raw.append(element.get_slice().data, element.get_slice().size);
        }
    } else {
        // Handle VARCHAR case
        json_str_raw = match_column->get(0).get_slice().to_string();
    }

    std::string json_str;
    // Check if it is compressed (Base64 encoded)
    if (is_likely_base64_compressed(json_str_raw)) {
        // Decode Base64
        std::vector<char> decoded_buffer(json_str_raw.size()); // Base64 decoded is always smaller
        int64_t decoded_size = base64_decode3(json_str_raw.c_str(), json_str_raw.size(), decoded_buffer.data());
        if (decoded_size < 0) {
            return Status::InvalidArgument("[" + function_name + "] Failed to decode Base64 data");
        }
        // Decompress
        std::string_view compressed_view(decoded_buffer.data(), decoded_size);
        if (!decompress_string(compressed_view, json_str)) {
            return Status::InvalidArgument("[" + function_name + "] Failed to decompress JSON data");
        }
    } else {
        // Use raw string as-is
        json_str = std::move(json_str_raw);
    }

    json json_array;
    try {
        if (!json::accept(json_str)) {
            return Status::InvalidArgument("[" + function_name + "] Invalid JSON format: " + json_str);
        }
        json_array = json::parse(json_str);
    } catch (const std::exception& e) {
        return Status::InvalidArgument(
                "[" + function_name + "] Exception (" + std::string(e.what()) + ") during parsing JSON string: " + json_str);
    }
    if (!json_array.is_array()) {
        return Status::InvalidArgument("[" + function_name + "] The JSON string is not an array.");
    }
    if constexpr (lt_is_string<LT>) {
        phmap::flat_hash_set<std::string> seen;
        for (const json& item: json_array) {
            if (item.is_null()) {
                state->match_has_null = true;
                continue;
            }
            const std::string s = item.get<std::string>();
            auto result = seen.insert(s);
            if (result.second) {
                state->strings.push_back(s);
            }
        }
        for (const auto& str: state->strings) {
            state->match_set.insert(Slice(str.data(), str.size()));
        }
    } else {
        for (const json& item: json_array) {
            if (item.is_null()) {
                state->match_has_null = true;
                continue;
            }
            state->match_set.insert(item.get<RunTimeCppType<LT>>());
        }
    }
    return Status::OK();
}

template<LogicalType LT>
Status CelonisInJson<LT>::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const InJsonStateFragmentLocal<LT>*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisInJson<LT>::in_json_non_constant_match([[maybe_unused]]FunctionContext* context,
                                                                  const Columns& columns) {
    return Status::NotSupported("The non-const version of CELONIS_IN_JSON is not supported.");
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisInJson<LT>::in_json_array_non_constant_match([[maybe_unused]]FunctionContext* context,
                                                                  const Columns& columns) {
    return Status::NotSupported("The non-const version of CELONIS_IN_JSON_ARRAY is not supported.");
}

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisInJson<LT>::in_json_constant_match([[maybe_unused]]FunctionContext* context, const Columns& columns) {
    const auto& value_column = columns[0];
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    ColumnViewer<LT> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    const auto* state = reinterpret_cast<const InJsonStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    for (auto row = 0; row < num_rows; ++row) {
        if (value_viewer.is_null(row)) {
            result.append(state->match_has_null);
        } else {
            result.append(state->match_set.count(value_viewer.value(row)) > 0);
        }
    }

    return result.build(all_const);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisInJson<LT>::in_json(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const InJsonStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisInJson<LT>::in_json_array(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const InJsonStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template
class CelonisInJson<TYPE_INT>;

template
class CelonisInJson<TYPE_BIGINT>;

template
class CelonisInJson<TYPE_DOUBLE>;

template
class CelonisInJson<TYPE_VARCHAR>;

} // namespace starrocks
