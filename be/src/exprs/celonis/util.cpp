#include "exprs/celonis/util.h"

#include "column/array_column.h"
#include "column/column_helper.h"

namespace starrocks {
const ArrayColumn& extract_array_column(const Column* input_column) {
    return *(down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_column)));
}
} // namespace starrocks
