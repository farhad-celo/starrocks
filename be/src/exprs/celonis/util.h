#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"

namespace starrocks {

const uint128_t XXHASH3_128_SEED = 0;

uint128_t xx_hash3_128(const void* key, int32_t len, uint128_t seed);

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
