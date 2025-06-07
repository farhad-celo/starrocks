#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"

namespace starrocks {

const uint128_t XXHASH3_128_SEED = 0;
// TODO: Think of a better way to handle this.
// used to represent NULL scalar value when computing hash value.
constexpr std::string_view XXHASH3_128_NULL_STRING = "_$CeL0nIs_ReSeRvEd_NuLl_";
// used to represent NULL array when computing hash value.
constexpr std::string_view XXHASH3_128_NULL_ARRAY_STRING = "_$CeL0nIs_ReSeRvEd_NuLl_aRrAy_";

const double EPS = 1e-9;
// maximum number of buckets output by CALC_(STRING_)BUCKET_COUNT/WIDTH_BOUNDARIES
const int64_t MAX_NUM_BUCKETS = 1000000;
const double HISTOGRAM_MIN_TARGET_QUANTILE = 0.05;
const double HISTOGRAM_MAX_TARGET_QUANTILE = 0.95;

template<LogicalType LT>
inline double to_histogram_value(const RunTimeCppType<LT>& value) {
    return static_cast<double>(value);
}

template<>
inline double to_histogram_value<TYPE_DATETIME>(const TimestampValue& value) {
    const auto epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
    // convert it to milliseconds
    return static_cast<double>(value.diff_microsecond(epoch) / 1000L);
}

template<LogicalType LT>
inline RunTimeCppType<LT> from_histogram_value(const double& value) {
    return static_cast<RunTimeCppType<LT>>(value);
}

template<>
inline TimestampValue from_histogram_value<TYPE_DATETIME>(const double& millis) {
    TimestampValue result;
    result.from_unix_second(static_cast<int64_t>(millis) / 1000L);
    return result.add<TimeUnit::MILLISECOND>(static_cast<int64_t>(millis) % 1000L);
}

uint128_t xx_hash3_128(const void* key, int32_t len, uint128_t seed);

bool is_ratio_invalid(double ratio);

int128_t safe_abs(int128_t value);

int64_t safe_abs(int64_t value);

// Casts 'input_column' as an ArrayColumn (removing Nullable wrapper if present).
const ArrayColumn& extract_array_column(const Column* input_column);

// Contains the representation of array column.
struct UnnestedArrayData {
    // Flattened array elements
    const Column* elements = nullptr;
    // Offsets (indicating new array start)
    const UInt32Column* offsets = nullptr;
    // Null indicators for NULL arrays, can be null
    const NullColumn::Container* null_arrays = nullptr;
    // Null indicators for NULL elements, can be null
    const NullColumn::Container* null_elements = nullptr;
};

UnnestedArrayData prepare_array_input(const Column* input_array);

std::string double_to_string(double value, int precision);

} // namespace starrocks
