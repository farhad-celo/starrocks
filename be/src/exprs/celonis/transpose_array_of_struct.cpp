#include "exprs/celonis/transpose_array_of_struct.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

StatusOr<ColumnPtr> CelonisTransposeArrayOfStruct::transpose_array_of_struct(starrocks::FunctionContext* context,
                                                                             const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const size_t n_rows = columns[0]->size();
    ColumnPtr res = context->create_column(context->get_return_type(), true);
    auto null_column = down_cast<NullableColumn*>(res.get());
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()))->fields_column();
    auto n_fields = fields.size();
    std::vector<ArrayColumn*> array_columns;
    array_columns.reserve(n_fields);
    for (auto i = 0; i < n_fields; ++i) {
        array_columns.push_back(down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[i].get())));
    }
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            res->append_nulls(1);
            continue;
        }
        null_column->null_column_data().emplace_back(0);
        auto array = columns[0]->get(row).get_array();
        const auto size = array.size();
        size_t num_added = 0;
        for (auto i = 0; i < size; ++i) {
            if (array[i].is_null()) {
                continue;
            }
            ++num_added;
            const auto& datums = array[i].get_struct();
            DCHECK_EQ(n_fields, datums.size());
            for (auto j = 0; j < n_fields; ++j) {
                array_columns[j]->elements_column()->append_datum(datums[j]);
            }
        }
        for (auto j = 0; j < n_fields; ++j) {
            auto& offsets = array_columns[j]->offsets_column()->get_data();
            offsets.push_back(offsets.back() + num_added);
            if (fields[j]->is_nullable()) {
                down_cast<NullableColumn*>(fields[j].get())->null_column_data().emplace_back(0);
            }
        }
    }
    return res;
}

} // namespace starrocks
