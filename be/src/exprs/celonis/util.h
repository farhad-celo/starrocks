#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"

namespace starrocks {

const uint128_t XXHASH3_128_SEED = 0;
const double EPS = 1e-9;

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

} // namespace starrocks
