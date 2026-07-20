#pragma once

namespace starrocks {

class ArrayColumn;
class Column;

// Casts 'input_column' as an ArrayColumn (removing Nullable wrapper if present).
const ArrayColumn& extract_array_column(const Column* input_column);
} // namespace starrocks
