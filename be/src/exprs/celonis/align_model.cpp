#include "exprs/celonis/align_model.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"

using celonis::accelerator::operators::process::align_model::AlignModelHelper;
using row_id = int32_t;

namespace starrocks {

namespace {
void AddArray(ColumnPtr column, std::vector<std::string> input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto &entry: input) {
        datum.emplace_back(Slice(entry));
    }
    column->append_datum(datum);
}

void AddArray(ColumnPtr column, std::vector<row_id> input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto &entry: input) {
        datum.emplace_back(static_cast<int64_t>(entry));
    }
    column->append_datum(datum);
}

void AddArray(ColumnPtr column, std::vector<std::optional<size_t>> input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto &entry: input) {
        if (entry.has_value()) {
            datum.emplace_back(static_cast<int64_t>(entry.value()));
        } else {
            datum.emplace_back();
        }
    }
    column->append_datum(datum);
}
} // namespace

StatusOr<ColumnPtr> CelonisAlignModel::align_model(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    if (columns[0]->only_null()) {
        return Status::InvalidArgument("input_array should not be null.");
    }
    auto json_bpmn_model_description =
            ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(1)).to_string();
    DCHECK(ColumnHelper::get_data_column(columns[0].get())->is_array());

    ColumnPtr res = context->create_column(context->get_return_type(), false);

    StructColumn* st = down_cast<StructColumn*>(res.get());
    auto fields = st->fields_column();

    size_t chunk_size = columns[0]->size();
    ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
    auto* src_data_column = src_column.get();
    if (src_column->is_nullable()) {
        if (src_column->has_null()) {
            return Status::InvalidArgument("input_array should not be null.");
        }
        const auto *src_nullable_column = down_cast<const NullableColumn *>(src_column.get());
        src_data_column = src_nullable_column->data_column().get();
    }
    const auto& src_elements = down_cast<const ArrayColumn*>(src_data_column)->elements();
    const auto& src_offsets = down_cast<const ArrayColumn*>(src_data_column)->offsets().get_data().data();

    std::vector<std::vector<std::string>> variants;
    variants.reserve(chunk_size);

    for (size_t row = 0; row < chunk_size; row++) {
        std::vector<std::string> variant;
        auto start = src_offsets[row];
        auto end = src_offsets[row + 1];
        variant.reserve(end - start);
        for (size_t i = start; i < end; ++i) {
            variant.push_back(src_elements.get(i).get_slice().to_string());
        }
        variants.push_back(std::move(variant));
    }

    AlignModelHelper helper;
    RETURN_IF_ERROR(helper.execute(variants, json_bpmn_model_description));
    const auto& result_table = helper.result_table();

    const auto& variant_col = result_table.column<std::vector<std::string>>("variant");
    const auto& alignment_model_vertex_id =
            result_table.column<std::vector<std::optional<size_t>>>("alignment_model_vertex_id");
    const auto& alignment_vertex_label = result_table.column<std::vector<std::string>>("alignment_vertex_label");
    const auto& alignment_move_type = result_table.column<std::vector<std::string>>("alignment_move_type");
    const auto& alignment_activity_index = result_table.column<std::vector<row_id>>("alignment_activity_index");
    const auto& association_edge_class = result_table.column<std::vector<row_id>>("association_edge_class");
    const auto& association_alignment_index = result_table.column<std::vector<row_id>>("association_alignment_index");
    const auto& edge_class_id = result_table.column<std::vector<row_id>>("edge_class_id");
    const auto& edge_class_type = result_table.column<std::vector<std::string>>("edge_class_type");

    for (size_t row = 0; row < chunk_size; row++) {
        AddArray(fields[0], variant_col[row]);
        AddArray(fields[1], alignment_model_vertex_id[row]);
        AddArray(fields[2], alignment_vertex_label[row]);
        AddArray(fields[3], alignment_move_type[row]);
        AddArray(fields[4], alignment_activity_index[row]);
        AddArray(fields[5], association_edge_class[row]);
        AddArray(fields[6], association_alignment_index[row]);
        AddArray(fields[7], edge_class_id[row]);
        AddArray(fields[8], edge_class_type[row]);
    }
    return res;
}

} // namespace starrocks