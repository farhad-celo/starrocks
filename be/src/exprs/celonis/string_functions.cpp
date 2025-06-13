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
#include "column/hash_set.h"
#include "util/phmap/phmap.h"
#include "util/xxh3.h"
#include "util/utf8.h"
#include "exprs/celonis/util.h"
#include "exprs/builtin_functions.h"
#include "exprs/unary_function.h"
#include "util/faststring.h"

namespace starrocks {

namespace {

constexpr std::string_view CELONIS_XX_HASH3_128_V2 = "CELONIS_XX_HASH3_128_V2";
constexpr std::string_view CELONIS_XX_HASH3_128_V3 = "CELONIS_XX_HASH3_128_V3";
constexpr std::string_view CELONIS_XX_HASH3_128_V4 = "CELONIS_XX_HASH3_128_V4";
constexpr std::string_view CELONIS_XX_HASH3_96 = "CELONIS_XX_HASH3_96";

Status xxh3_128bits_update(XXH3_state_t* state, const void* input, size_t len) {
    XXH_errorcode code = XXH3_128bits_update(state, input, len);
    // code != XXH_OK (e.g., state is not initialized correctly, state == NULL) should be very rare in practice.
    if (UNLIKELY(code != XXH_OK)) {
        return Status::InternalError("Update xxh3 state failed");
    }
    return Status::OK();
}

Status xxh3_128bits_update_v3(XXH3_state_t* state, const void* input, size_t len) {
    RETURN_IF_ERROR(xxh3_128bits_update(state, input, len));
    // Also hash the value "_" + std::string(len) + "_"
    char len_buffer[32];  // enough for any size_t value
    len_buffer[0] = '_';
    char* p = len_buffer + 1;
    size_t temp_len = len;

    // Extract digits in reverse order
    char* digit_start = p;
    do {
        *p++ = '0' + (temp_len % 10);
        temp_len /= 10;
    } while (temp_len > 0);

    // Reverse the digits in place
    char* digit_end = p - 1;
    while (digit_start < digit_end) {
        char temp = *digit_start;
        *digit_start++ = *digit_end;
        *digit_end-- = temp;
    }
    *p++ = '_';
    RETURN_IF_ERROR(xxh3_128bits_update(state, len_buffer, p - len_buffer));
    return Status::OK();
}

Status xxh3_128bits_update_v4(XXH3_state_t* state, const void* input, size_t len) {
    uint32_t new_len = static_cast<uint32_t>(len);
    RETURN_IF_ERROR(xxh3_128bits_update(state, &new_len, sizeof(uint32_t)));
    return xxh3_128bits_update(state, input, len);
}

template<std::string_view const& reserved_str, std::string_view const& function_name>
inline Status validate_slice_template(const Slice& slice) {
    if (reserved_str.size() == slice.size &&
        std::memcmp(reserved_str.data(), slice.data, slice.size) == 0) {
        return Status::InvalidArgument(
                std::string(function_name)
                        .append(": string value conflicts with the reserved string '")
                        .append(reserved_str)
                        .append("'.")
                        .c_str());
    }
    return Status::OK();
}


template<bool hash96, bool enable_validation, std::string_view const& function_name>
StatusOr<ColumnPtr> xx_hash3_helper(starrocks::FunctionContext* context, const starrocks::Columns& columns,
                                    Status (* hash_update_func)(XXH3_state_t*, const void*, size_t)) {
    DCHECK(columns.size() >= 1);
    const auto [all_const, n_rows] = ColumnHelper::num_packed_rows(columns);
    const uint128_t default_xxhash_seed = XXHASH3_128_SEED;
    XXH3_state_t init_state;
    XXH_errorcode code = XXH3_128bits_reset_withSeed(&init_state, default_xxhash_seed);
    if (UNLIKELY(code != XXH_OK)) {
        return Status::InternalError(std::string(function_name).append(": init xxh3 state failed"));
    }
    std::vector<XXH3_state_t> states;
    states.reserve(n_rows);

    if (context->get_arg_type(0)->type == TYPE_ARRAY) {
        states.assign(n_rows, init_state);
        DCHECK_EQ(1, columns.size());
        // columns[0] is NULL literal
        if (columns[0]->only_null()) {
            XXH3_state_t null_array_hash;
            XXH_errorcode reset_code = XXH3_128bits_reset_withSeed(&null_array_hash, default_xxhash_seed);
            if (reset_code != XXH_OK) {
                return Status::InternalError(std::string(function_name).append(": init xxh3 state failed"));
            }
            RETURN_IF_ERROR(hash_update_func(&null_array_hash, XXHASH3_128_NULL_ARRAY_STRING.data(),
                                             XXHASH3_128_NULL_ARRAY_STRING.size()));
            XXH128_hash_t value = XXH3_128bits_digest(&null_array_hash);
            int128_t res = ((int128_t) value.high64 << 64) | (uint64_t) value.low64;
            auto result_column = context->create_column(context->get_return_type(), false);
            result_column->append_datum(res);
            // We need to return a const column here, otherwise function_call_expr will not resize it correctly.
            return ConstColumn::create(std::move(result_column), n_rows);
        }
        ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
        UnnestedArrayData string_data = prepare_array_input(array_column.get());
        const auto& slices = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
                *string_data.elements).get_data().data();
        const auto& offsets = string_data.offsets->get_data().data();
        for (size_t row = 0; row < n_rows; ++row) {
            if (columns[0]->is_null(row)) {
                RETURN_IF_ERROR(hash_update_func(&states[row], XXHASH3_128_NULL_ARRAY_STRING.data(),
                                                 XXHASH3_128_NULL_ARRAY_STRING.size()));
                continue;
            }
            const auto start = offsets[row];
            const auto end = offsets[row + 1];
            for (auto i = start; i < end; ++i) {
                const bool is_null = string_data.null_elements != nullptr && (*string_data.null_elements)[i] != 0;
                if (!is_null) {
                    if constexpr (enable_validation) {
                        // validate that the input string does not conflict with the reserved null string.
                        RETURN_IF_ERROR((validate_slice_template<XXHASH3_128_NULL_STRING, function_name>(slices[i])));
                        RETURN_IF_ERROR(
                                (validate_slice_template<XXHASH3_128_NULL_ARRAY_STRING, function_name>(slices[i])));
                    }
                    RETURN_IF_ERROR(hash_update_func(&states[row], slices[i].data, slices[i].size));
                } else {
                    RETURN_IF_ERROR(hash_update_func(&states[row], XXHASH3_128_NULL_STRING.data(),
                                                     XXHASH3_128_NULL_STRING.size()));
                }
            }
        }
    } else {
        size_t num_const_columns = 0;
        while (num_const_columns < columns.size()) {
            if (columns[num_const_columns]->is_constant()) {
                ++num_const_columns;
            } else {
                break;
            }
        }
        std::vector<ColumnViewer<TYPE_VARCHAR>> column_viewers;
        column_viewers.reserve(columns.size());
        for (const auto& column: columns) {
            column_viewers.emplace_back(column);
        }
        // Handle the leading constant columns
        if (n_rows > 0) {
            for (auto i = 0; i < num_const_columns; ++i) {
                const auto& viewer = column_viewers[i];
                if (!viewer.is_null(0)) {
                    if constexpr (enable_validation) {
                        RETURN_IF_ERROR(
                                (validate_slice_template<XXHASH3_128_NULL_STRING, function_name>(viewer.value(0))));
                    }
                    RETURN_IF_ERROR(hash_update_func(&(init_state), viewer.value(0).data, viewer.value(0).size));
                } else {
                    RETURN_IF_ERROR(hash_update_func(&(init_state), XXHASH3_128_NULL_STRING.data(),
                                                     XXHASH3_128_NULL_STRING.size()));
                }
            }
        }
        states.assign(n_rows, init_state);
        for (auto i = num_const_columns; i < columns.size(); ++i) {
            const auto& viewer = column_viewers[i];
            for (size_t row = 0; row < n_rows; ++row) {
                if (!viewer.is_null(row)) {
                    if constexpr (enable_validation) {
                        RETURN_IF_ERROR(
                                (validate_slice_template<XXHASH3_128_NULL_STRING, function_name>(viewer.value(row))));
                    }
                    RETURN_IF_ERROR(hash_update_func(&(states[row]), viewer.value(row).data, viewer.value(row).size));
                } else {
                    RETURN_IF_ERROR(hash_update_func(&(states[row]), XXHASH3_128_NULL_STRING.data(),
                                                     XXHASH3_128_NULL_STRING.size()));
                }
            }
        }
    }
    if constexpr (!hash96) {
        ColumnBuilder<TYPE_LARGEINT> builder(n_rows);
        for (int row = 0; row < n_rows; ++row) {
            XXH128_hash_t value = XXH3_128bits_digest(&states[row]);
            int128_t res = ((int128_t) value.high64 << 64) | (uint64_t) value.low64;
            builder.append(res, false);
        }
        return builder.build(all_const);
    } else {
        ColumnBuilder<TYPE_VARCHAR> builder(n_rows);
        char buf[12];
        for (int row = 0; row < n_rows; ++row) {
            XXH128_hash_t value = XXH3_128bits_digest(&states[row]);
            // Use all the 8 bytes from high64
            std::memcpy(buf, &value.high64, 8);
            // Use the top 4 bytes from low64.
            std::memcpy(buf + 8, &value.low64, 4);
            builder.append(Slice(buf, 12));
        }
        return builder.build(all_const);
    }
}

}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_128_v2(starrocks::FunctionContext* context,
                                                            const starrocks::Columns& columns) {
    return xx_hash3_helper<false, true, CELONIS_XX_HASH3_128_V2>(context, columns, xxh3_128bits_update);
}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_128_v3(starrocks::FunctionContext* context,
                                                            const starrocks::Columns& columns) {
    return xx_hash3_helper<false, true, CELONIS_XX_HASH3_128_V3>(context, columns, xxh3_128bits_update_v3);
}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_128_v4(starrocks::FunctionContext* context,
                                                            const starrocks::Columns& columns) {
    if (context->get_arg_type(0)->type != TYPE_ARRAY && columns.size() == 1) {
        return xx_hash3_helper<false, false, CELONIS_XX_HASH3_128_V4>(context, columns, xxh3_128bits_update);
    }
    return xx_hash3_helper<false, false, CELONIS_XX_HASH3_128_V4>(context, columns, xxh3_128bits_update_v4);
}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_96(starrocks::FunctionContext* context,
                                                        const starrocks::Columns& columns) {
    return xx_hash3_helper<true, true, CELONIS_XX_HASH3_96>(context, columns, xxh3_128bits_update_v3);
}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_128(starrocks::FunctionContext* context,
                                                         const starrocks::Columns& columns) {
    const size_t row_size = columns[0]->size();
    const uint128_t default_xxhash_seed = XXHASH3_128_SEED;
    std::vector<uint128_t> seeds_vec(row_size, default_xxhash_seed);
    if (context->get_arg_type(0)->type == TYPE_ARRAY) {
        // columns[0] is NULL literal
        if (columns[0]->only_null()) {
            const auto null_array_hash = ::starrocks::xx_hash3_128(XXHASH3_128_NULL_ARRAY_STRING.data(),
                                                                   XXHASH3_128_NULL_ARRAY_STRING.size(),
                                                                   default_xxhash_seed);
            auto result_column = context->create_column(context->get_return_type(), false);
            result_column->append_datum(null_array_hash);
            return ConstColumn::create(std::move(result_column), row_size);
        }
        ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
        UnnestedArrayData string_data = prepare_array_input(array_column.get());
        const auto& strings = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
                *string_data.elements).get_data().data();
        const auto& offsets = string_data.offsets->get_data().data();
        for (size_t row = 0; row < row_size; ++row) {
            if (columns[0]->is_null(row)) {
                seeds_vec[row] = ::starrocks::xx_hash3_128(XXHASH3_128_NULL_ARRAY_STRING.data(),
                                                           XXHASH3_128_NULL_ARRAY_STRING.size(), seeds_vec[row]);
                continue;
            }
            const auto start = offsets[row];
            const auto end = offsets[row + 1];
            for (auto i = start; i < end; ++i) {
                const bool is_null = string_data.null_elements != nullptr && (*string_data.null_elements)[i] != 0;
                uint128_t seed = seeds_vec[row];
                if (is_null) {
                    seeds_vec[row] = ::starrocks::xx_hash3_128(XXHASH3_128_NULL_STRING.data(),
                                                               XXHASH3_128_NULL_STRING.size(), seed);
                } else {
                    Slice slice = strings[i];
                    if (XXHASH3_128_NULL_STRING.size() == slice.size &&
                        XXHASH3_128_NULL_STRING.compare(0, XXHASH3_128_NULL_STRING.size(), slice.data, slice.size) ==
                        0) {
                        return Status::InvalidArgument(
                                ("CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL string '" +
                                 std::string(XXHASH3_128_NULL_STRING) + "'.").c_str());
                    }
                    if (XXHASH3_128_NULL_ARRAY_STRING.size() == slice.size &&
                        XXHASH3_128_NULL_ARRAY_STRING.compare(0, XXHASH3_128_NULL_ARRAY_STRING.size(), slice.data,
                                                              slice.size) == 0) {
                        return Status::InvalidArgument(
                                ("CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL array string '" +
                                 std::string(XXHASH3_128_NULL_ARRAY_STRING) + "'.").c_str());
                    }
                    seeds_vec[row] = ::starrocks::xx_hash3_128(slice.data, slice.size, seed);
                }
            }
        }
    } else {
        std::vector<ColumnViewer<TYPE_VARCHAR>> column_viewers;
        column_viewers.reserve(columns.size());
        for (const auto& column: columns) {
            column_viewers.emplace_back(column);
        }
        for (const auto& viewer: column_viewers) {
            for (size_t row = 0; row < row_size; ++row) {
                uint128_t seed = seeds_vec[row];
                if (viewer.is_null(row)) {
                    seeds_vec[row] = ::starrocks::xx_hash3_128(XXHASH3_128_NULL_STRING.data(),
                                                               XXHASH3_128_NULL_STRING.size(), seed);
                } else {
                    auto slice = viewer.value(row);
                    if (XXHASH3_128_NULL_STRING.size() == slice.size &&
                        XXHASH3_128_NULL_STRING.compare(0, XXHASH3_128_NULL_STRING.size(), slice.data, slice.size) ==
                        0) {
                        return Status::InvalidArgument(
                                ("CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL string '" +
                                 std::string(XXHASH3_128_NULL_STRING) + "'.").c_str());
                    }
                    seeds_vec[row] = ::starrocks::xx_hash3_128(slice.data, slice.size, seed);
                }
            }
        }
    }
    ColumnBuilder<TYPE_LARGEINT> builder(row_size);
    std::vector<bool> is_null_vec(row_size, false);
    for (int row = 0; row < row_size; ++row) {
        builder.append(seeds_vec[row], is_null_vec[row]);
    }
    return builder.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisStringFunctions::xx_hash3_128_nullable(starrocks::FunctionContext* context,
                                                                  const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    size_t row_size = columns[0]->size();
    const uint128_t default_xxhash_seed = XXHASH3_128_SEED;
    XXH3_state_t init_state;
    XXH_errorcode code = XXH3_128bits_reset_withSeed(&init_state, default_xxhash_seed);
    if (UNLIKELY(code != XXH_OK)) {
        return Status::InternalError("CELONIS_XX_HASH3_128_NULLABLE: init xxh3 state failed");
    }
    std::vector<XXH3_state_t> states(row_size, init_state);
    std::vector<bool> is_null_vec(row_size, false);

    if (context->get_arg_type(0)->type == TYPE_ARRAY) {
        DCHECK_EQ(1, columns.size());
        if (columns[0]->only_null()) {
            auto result_column = context->create_column(context->get_return_type(), true);
            result_column->append_nulls(row_size);
            return result_column;
        }
        ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
        UnnestedArrayData string_data = prepare_array_input(array_column.get());
        const auto& strings = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
                *string_data.elements).get_data().data();
        const auto& offsets = string_data.offsets->get_data().data();
        for (size_t row = 0; row < row_size; ++row) {
            if (columns[0]->is_null(row)) {
                is_null_vec[row] = true;
                continue;
            }
            const auto start = offsets[row];
            const auto end = offsets[row + 1];
            for (auto i = start; i < end; ++i) {
                if (string_data.null_elements != nullptr && (*string_data.null_elements)[i] != 0) {
                    is_null_vec[row] = true;
                    break;
                }
                Slice slice = strings[i];
                RETURN_IF_ERROR(
                        xxh3_128bits_update_v3(&states[row], slice.data, slice.size));
            }
        }
    } else {
        std::vector<ColumnViewer<TYPE_VARCHAR>> column_viewers;
        column_viewers.reserve(columns.size());
        for (const auto& column: columns) {
            column_viewers.emplace_back(column);
        }
        for (const auto& viewer: column_viewers) {
            for (size_t row = 0; row < row_size; ++row) {
                if (is_null_vec[row]) {
                    continue;
                }
                if (viewer.is_null(row)) {
                    is_null_vec[row] = true;
                    continue;
                }
                auto slice = viewer.value(row);
                RETURN_IF_ERROR(
                        xxh3_128bits_update_v3(&states[row], slice.data, slice.size));
            }
        }
    }
    ColumnBuilder<TYPE_LARGEINT> builder(row_size);
    for (int row = 0; row < row_size; ++row) {
        XXH128_hash_t value = XXH3_128bits_digest(&states[row]);
        int128_t res = ((int128_t) value.high64 << 64) | (uint64_t) value.low64;
        builder.append(res, is_null_vec[row]);
    }
    return builder.build(ColumnHelper::is_all_const(columns));
}

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

    const auto replace_col = context->get_constant_column(2);
    Slice replace = ColumnHelper::get_const_value<TYPE_VARCHAR>(replace_col);

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
    DCHECK_EQ(3, columns.size());
    const auto* state = reinterpret_cast<const CelonisTranslateState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    DCHECK(state != nullptr);
    if (state->pattern_chars.empty()) {
        return columns[0];
    }
    const auto& translate_mapping = state->translate_mapping;

    auto str_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);

    auto size = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> result(size);
    faststring result_str;
    for (int row = 0; row < size; ++row) {
        if (str_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        const auto str_value = str_viewer.value(row);
        result_str.clear();
        result_str.reserve(str_value.get_size());

        int char_size = 0;
        for (const char* str_p = str_value.get_data(), * str_end = str_p + str_value.get_size();
             str_p < str_end; str_p += char_size) {
            char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<uint8_t>(*str_p)];

            const auto cit = translate_mapping.find(Slice(str_p, char_size));
            if (cit == translate_mapping.end()) {
                result_str.append(str_p, char_size);
            } else {
                result_str.append(cit->second.data, cit->second.size);
            }
        }
        result.append(Slice(result_str.data(), result_str.size()));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

struct StringCaseLowerFunction {
public:
    template<LogicalType Type, LogicalType ResultType>
    static ColumnPtr evaluate(const ColumnPtr& column) {
        auto result = column->clone_shared();
        auto dst = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(result.get()));
        auto& dst_bytes = dst->get_bytes();

        const size_t size = dst_bytes.size();
        char* begin = (char*) (dst_bytes.data());
        char* end = (char*) (begin + size);

        // for UTF-8, the leading bytes and the continuation bytes do not share values.
        for (char* ptr = begin; ptr < end; ++ptr) {
            if ('A' <= (*ptr) && (*ptr) <= 'Z') {
                *ptr = (*ptr) + 32;
                continue;
            }
            // Character: Ä | UTF-8 Bytes: ['0xC3', '0x84']
            // Character: Ö | UTF-8 Bytes: ['0xC3', '0x96']
            // Character: Ü | UTF-8 Bytes: ['0xC3', '0x9C']
            if ((*ptr) == '\xC3' && (ptr + 1) < end &&
                ((*(ptr + 1) == '\x84') || (*(ptr + 1) == '\x96') || (*(ptr + 1) == '\x9C'))) {
                *(ptr + 1) = *(ptr + 1) + 32;
            }
        }
        return result;
    }
};

StatusOr<ColumnPtr> CelonisStringFunctions::lower([[maybe_unused]]FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    return VectorizedUnaryFunction<StringCaseLowerFunction>::evaluate<TYPE_VARCHAR>(columns[0]);
}

struct StringCaseUpperFunction {
public:
    template<LogicalType Type, LogicalType ResultType>
    static ColumnPtr evaluate(const ColumnPtr& column) {
        auto result = column->clone_shared();
        auto dst = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(result.get()));
        auto& dst_bytes = dst->get_bytes();

        const size_t size = dst_bytes.size();
        char* begin = (char*) (dst_bytes.data());
        char* end = (char*) (begin + size);

        // for UTF-8, the leading bytes and the continuation bytes do not share values.
        for (char* ptr = begin; ptr < end; ++ptr) {
            if ('a' <= (*ptr) && (*ptr) <= 'z') {
                *ptr = (*ptr) - 32;
                continue;
            }
            // Character: ä | UTF-8 Bytes: ['0xC3', '0xA4']
            // Character: ö | UTF-8 Bytes: ['0xC3', '0xB6']
            // Character: ü | UTF-8 Bytes: ['0xC3', '0xBC']
            if ((*ptr) == '\xC3' && (ptr + 1) < end &&
                ((*(ptr + 1) == '\xA4') || (*(ptr + 1) == '\xB6') || (*(ptr + 1) == '\xBC'))) {
                *(ptr + 1) = *(ptr + 1) - 32;
            }
        }
        return result;
    }
};

StatusOr<ColumnPtr> CelonisStringFunctions::upper([[maybe_unused]]FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    return VectorizedUnaryFunction<StringCaseUpperFunction>::evaluate<TYPE_VARCHAR>(columns[0]);
}

StatusOr<ColumnPtr> CelonisStringFunctions::sanitize_invalid_utf8(starrocks::FunctionContext* context,
                                                                  const starrocks::Columns& columns) {
    auto str_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const auto [all_const, n_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_VARCHAR> result(n_rows);

    constexpr char REPLACEMENT_CHAR{'?'};
    faststring sanitized;
    char buffer[4];
    for (int row = 0; row < n_rows; ++row) {
        if (str_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        // Sanitization logic is copied from query-engine/src/main/native/cpm-accelerator/modules/format/src/utf/utf_utils.cpp
        // in cpm-query-engine repository.
        auto input = std::string_view(str_viewer.value(row));
        size_t found = input.find('\0');
        input = input.substr(0, found);
        sanitized.clear();
        sanitized.reserve(input.length());
        for (const auto* itr{input.begin()}; itr != input.end();) {
            const auto decoded{boost::locale::utf::utf_traits<char>::decode(itr, input.end())};
            if (decoded == boost::locale::utf::illegal || decoded == boost::locale::utf::incomplete) {
                sanitized.push_back(REPLACEMENT_CHAR);
            } else {
                char* end = boost::locale::utf::utf_traits<char>::encode(decoded, buffer);
                sanitized.append(buffer, end - buffer);
            }
        }
        result.append(Slice(sanitized.data(), sanitized.size()));
    }

    return result.build(all_const);
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

std::string_view trim_spaces(const std::string_view& str) {
    // Find the first non-space character from the beginning
    auto start = std::find_if_not(str.begin(), str.end(), [](char c) {
        return std::isspace(c);
    });

    // Find the first non-space character from the end (reverse iteration)
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](char c) {
        return std::isspace(c);
    }).base(); // .base() converts reverse_iterator back to iterator

    if (start >= end) {
        return "";
    }

    return std::string_view(&*start, std::distance(start, end));
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


static std::optional<double> to_double(std::stringstream& ss, const std::string_view& input_string) {
    std::string_view trimmed_input_string = trim_spaces(input_string);
    ss.clear();   // Clear any existing error flags
    ss.str("");   // Clear the content of the internal buffer
    ss.seekp(0);  // Reset the put pointer to the beginning of the stream
    ss.seekg(0);  // Reset the get pointer to the beginning of the stream
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

std::optional<int64_t> to_int64(const std::string_view& str) {
    if (str.empty()) {
        return std::nullopt;
    }
    size_t index = 0;
    bool negative = false;
    // Handle sign
    if (str[index] == '-') {
        negative = true;
        index++;
    } else if (str[index] == '+') {
        index++;
    }
    // Must have at least one digit
    if (index >= str.size() || !std::isdigit(str[index])) {
        return std::nullopt;
    }
    int64_t result = 0;
    const int64_t pos_limit = std::numeric_limits<int64_t>::max();
    const int64_t neg_limit = std::numeric_limits<int64_t>::min();
    const int64_t limit = negative ? neg_limit : -pos_limit;
    const int64_t limit_before = limit / 10;

    // Parse digits
    while (index < str.size() && std::isdigit(str[index])) {
        if (result < limit_before) {
            return std::nullopt;  // Overflow check
        }
        result *= 10;
        int digit = str[index] - '0';
        if (result < limit + digit) {
            return std::nullopt;  // Overflow check
        }
        result -= digit;
        index++;
    }

    // Handle decimal part if present
    if (index < str.size() && str[index] == '.') {
        index++;
        // Skip decimal digits
        while (index < str.size() && std::isdigit(str[index])) {
            index++;
        }
    }
    // Check if we've consumed the entire string
    if (index != str.size()) {
        return std::nullopt;
    }
    return negative ? result : -result;
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
        std::string_view input_string = std::string_view(input_string_viewer.value(i));
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
    // std::stringstream is about 2.5x faster than atof on large inputs
    std::stringstream ss{};
    const std::locale& en_us_utf8_locale = get_locale();
    std::locale loc_with_thousands_sep{en_us_utf8_locale, new comma_separator_facet};
    // Set the numeric locale to "en_US.UTF-8" for proper parsing
    ss.imbue(loc_with_thousands_sep);
    for (int i = 0; i < size; ++i) {
        if (input_string_viewer.is_null(i)) {
            res.append_null();
            continue;
        }
        std::string_view input_string = std::string_view(input_string_viewer.value(i));
        std::optional<double> result = to_double(ss, input_string);
        if (result.has_value()) {
            res.append(std::move(result.value()));
        } else {
            res.append_null();
        }

    }
    return res.build(ColumnHelper::is_all_const(columns));
}

std::string to_lower_utf8(const std::string& input) {
    std::string rv = input;
    const auto size = rv.size();
    // for UTF-8, the leading bytes and the continuation bytes do not share values.
    for (auto i = 0; i < size; ++i) {
        if (rv[i] >= 'A' && rv[i] <= 'Z') {
            rv[i] += 32;
            continue;
        }
        // Character: Ä | UTF-8 Bytes: ['0xC3', '0x84']
        // Character: Ö | UTF-8 Bytes: ['0xC3', '0x96']
        // Character: Ü | UTF-8 Bytes: ['0xC3', '0x9C']
        if (rv[i] == '\xC3' && (i + 1 < size) &&
            ((rv[i + 1] == '\x84') || (rv[i + 1] == '\x96') || (rv[i + 1] == '\x9C'))) {
            rv[i + 1] += 32;
        }
    }
    return rv;
}

static bool match_helper(const std::string& input, const std::string& pattern, int i, int j) {
    if (j == pattern.length()) { // End of pattern
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

bool contains_wildcard(const std::string& pattern) {
    int backslash_count = 0;
    for (char ch: pattern) {
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

std::string remove_escape(const std::string& str) {
    std::string rv;
    int backslash_count = 0;
    for (char c: str) {
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
        return to_lower_utf8(input).find(to_lower_utf8(remove_escape(pattern))) != std::string::npos;
    } else {
        return match_helper(input, pattern, 0, 0);
    }
}

struct CelonisInLikeState {
    CelonisInLikeState() {}

    std::vector<std::string> patterns;
    std::vector<bool> case_insensitives;
    bool has_null = false;
    bool is_null = false;
    ScalarFunction function;
};

Status CelonisStringFunctions::in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
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
    for (const auto& pattern_datum: pattern_array) {
        if (pattern_datum.is_null()) {
            state->has_null = true;
            continue;
        }
        const std::string raw_pattern = pattern_datum.get_slice().to_string();
        bool seen = !pattern_seen.insert(raw_pattern).second;
        if (seen) {
            continue;
        }
        const bool has_wildcard = contains_wildcard(raw_pattern);
        bool case_insensitive = !has_wildcard;
        const std::string modified_pattern = has_wildcard ? raw_pattern : to_lower_utf8(remove_escape(raw_pattern));
        state->patterns.push_back(modified_pattern);
        state->case_insensitives.push_back(case_insensitive);
    }
    return Status::OK();
}

Status CelonisStringFunctions::in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<CelonisInLikeState*>(context->get_function_state(scope));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisStringFunctions::in_like_non_constant_patterns(starrocks::FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[1] });
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    // Handle constant patterns column here. SR may send a const patterns column chunk to this function.
    ColumnPtr patterns_column = ColumnHelper::unpack_and_duplicate_const_column(columns[1]->size(), columns[1]);
    UnnestedArrayData pattern_data = prepare_array_input(patterns_column.get());
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
            if (string_match(input_string, pattern)) {
                found_match = 1L;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisStringFunctions::in_like_constant_patterns([[maybe_unused]] FunctionContext* context,
                                                  const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const auto* state = reinterpret_cast<const CelonisInLikeState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    size_t n_rows = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
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
                    lower_input_string = to_lower_utf8(input_string);
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
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisStringFunctions::in_like([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    const auto* state = reinterpret_cast<const CelonisInLikeState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

static int edit_distance(const std::string& str1, const std::string& str2) {
    const int len1 = str1.size();
    const int len2 = str2.size();
    std::vector<int> prev_row(len2 + 1);
    std::vector<int> curr_row(len2 + 1);
    // Initialize the previous row (first row in the dp table)
    std::iota(prev_row.begin(), prev_row.end(), 0);
    for (int i = 1; i <= len1; i++) {
        curr_row[0] = i;  // Initialize the first element of the current row
        for (int j = 1; j <= len2; j++) {
            int cost = (str1[i - 1] == str2[j - 1]) ? 0 : 1;
            curr_row[j] = std::min({prev_row[j] + 1,    // Deletion
                                    curr_row[j - 1] + 1, // Insertion
                                    prev_row[j - 1] + cost}); // Substitution
        }
        std::swap(prev_row, curr_row);
    }
    return prev_row[len2];
}

struct CelonisMatchStringsState {
    CelonisMatchStringsState() {}

    HashSet<std::string> match_strings;
    bool null_match_array = false;
    int64_t top_k = 1;
    std::string separator = ", ";
    ScalarFunction function;
};

Status
CelonisStringFunctions::match_strings_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new CelonisMatchStringsState();
    context->set_function_state(scope, state);

    auto match_strings_column = context->get_constant_column(1);
    auto top_k_column = context->get_constant_column(2);
    auto separator_column = context->get_constant_column(3);
    if (match_strings_column == nullptr || top_k_column == nullptr || separator_column == nullptr) {
        state->function = match_strings_non_constant;
        return Status::OK();
    }
    state->function = match_strings_constant;
    if (match_strings_column->empty()) {
        return Status::OK();
    }
    if (!top_k_column->is_null(0)) {
        state->top_k = ColumnHelper::get_const_value<TYPE_BIGINT>(top_k_column);
    }
    if (state->top_k <= 0) {
        return Status::InvalidArgument("CELONIS_MATCH_STRINGS: top_k must be positive.");
    }
    if (!separator_column->is_null(0)) {
        state->separator = ColumnHelper::get_const_value<TYPE_VARCHAR>(separator_column).to_string();
    }
    if (match_strings_column->is_null(0)) {
        state->null_match_array = true;
        return Status::OK();
    }
    auto match_string_array = match_strings_column->get(0).get_array();
    for (const auto& match_string: match_string_array) {
        if (match_string.is_null()) {
            continue;
        }
        const std::string match_str = match_string.get_slice().to_string();
        state->match_strings.insert(match_str);
    }
    return Status::OK();
}

Status
CelonisStringFunctions::match_strings_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<CelonisMatchStringsState*>(context->get_function_state(scope));
        delete state;
    }
    return Status::OK();
}

std::string
get_match_strings_result(const std::string& input_string, const HashSet<std::string>& match_strings, int64_t top_k,
                         const std::string& separator) {
    HashSet<char> char_set(input_string.begin(), input_string.end());
    const std::string chars(char_set.begin(), char_set.end());
    std::vector<std::pair<int, std::string>> pairs;
    for (const auto& match_string: match_strings) {
        if (match_string.find_first_of(chars) != std::string::npos) {
            pairs.emplace_back(edit_distance(input_string, match_string), match_string);
        }
    }
    top_k = std::min(top_k, static_cast<int64_t>(pairs.size()));
    std::partial_sort(pairs.begin(), pairs.begin() + top_k, pairs.end());
    std::string sep = "";
    std::string joined = "";
    for (const auto& p: pairs) {
        if (top_k-- > 0) {
            joined += sep;
            joined += p.second;
        } else {
            break;
        }
        sep = separator;
    }
    return joined;
}

StatusOr<ColumnPtr>
CelonisStringFunctions::match_strings_non_constant([[maybe_unused]] FunctionContext* context,
                                                   const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[1] });
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    ColumnPtr match_string_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[1]);
    UnnestedArrayData match_string_data = prepare_array_input(match_string_column.get());
    const auto& match_strings = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *match_string_data.elements).get_data().data();
    const auto& offsets = match_string_data.offsets->get_data().data();
    ColumnViewer top_k_viewer = ColumnViewer<TYPE_BIGINT>(columns[2]);
    ColumnViewer separator_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnBuilder<TYPE_VARCHAR> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string input_string = input_string_viewer.value(row).to_string();
        int64_t top_k = top_k_viewer.is_null(row) ? 1 : top_k_viewer.value(row);
        const std::string separator = separator_viewer.is_null(row) ? ", " : separator_viewer.value(row).to_string();
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        HashSet<std::string> match_string_set;
        match_string_set.reserve(end - start);
        for (auto i = start; i < end; ++i) {
            if (match_string_data.null_elements != nullptr && (*match_string_data.null_elements)[i] != 0) {
                continue;
            }
            match_string_set.insert(match_strings[i].to_string());
        }
        if (top_k <= 0) {
            return Status::InvalidArgument("CELONIS_MATCH_STRINGS: top_k must be positive.");
        }
        result.append(get_match_strings_result(input_string, match_string_set, top_k, separator));
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisStringFunctions::match_strings_constant([[maybe_unused]] FunctionContext* context,
                                               const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[1] });
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const auto* state = reinterpret_cast<const CelonisMatchStringsState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnBuilder<TYPE_VARCHAR> result(n_rows);
    phmap::flat_hash_map<Slice, std::string, SliceHashWithSeed<PhmapSeed1>, SliceEqual> cache;
    const int64_t top_k = state->top_k;
    const std::string& separator = state->separator;
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || state->null_match_array) {
            result.append_null();
            continue;
        }
        auto input_slice = input_string_viewer.value(row);
        auto it = cache.find(input_slice);
        if (it == cache.end()) {
            const auto match_result = get_match_strings_result(input_slice.to_string(), state->match_strings, top_k,
                                                               separator);
            cache.insert({input_slice, match_result});
            result.append(match_result);
        } else {
            result.append(it->second);
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisStringFunctions::match_strings([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    const auto* state = reinterpret_cast<const CelonisMatchStringsState*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
