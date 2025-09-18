#include "exprs/celonis/align_model_v2.h"

#include <optional>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "util.h"

using celonis::accelerator::operators::process::align_model::AlignModelHelper;
using row_id = int32_t;

namespace starrocks {

namespace {
void AddArray(const ColumnPtr& column, const std::vector<std::string>& input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto& entry : input) {
        datum.emplace_back(Slice(entry));
    }
    column->append_datum(datum);
}

void AddArray(const ColumnPtr& column, const std::vector<row_id>& input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto& entry : input) {
        datum.emplace_back(static_cast<int64_t>(entry));
    }
    column->append_datum(datum);
}

void AddArray(const ColumnPtr& column, const std::vector<std::optional<size_t>>& input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto& entry : input) {
        if (entry.has_value()) {
            datum.emplace_back(static_cast<int64_t>(entry.value()));
        } else {
            datum.emplace_back();
        }
    }
    column->append_datum(datum);
}

void AddNulls(Columns& fields) {
    for (auto& field : fields) {
        field->append_nulls(1);
    }
}

} // namespace

struct AlignModelStateFragmentLocal {
    std::string json_bpmn_model_description;
};

Status CelonisAlignModelV2::align_model_v2_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::InvalidArgument(
                    "The second parameter of celonis_align_model() only accepts a literal value");
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        auto state = new AlignModelStateFragmentLocal();
        // As of 2023-10-11, get_const_value() is not thread-safe. So it shouldn't be called in align_model().
        state->json_bpmn_model_description =
                ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(1)).to_string();
        context->set_function_state(scope, state);
    }

    return Status::OK();
}

Status CelonisAlignModelV2::align_model_v2_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* align_model_state_fragment_local = reinterpret_cast<const AlignModelStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete align_model_state_fragment_local;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisAlignModelV2::align_model_v2(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto* align_model_state_fragment_local = reinterpret_cast<const AlignModelStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    const auto& json_bpmn_model_description = align_model_state_fragment_local->json_bpmn_model_description;

    ColumnPtr res = context->create_column(context->get_return_type(), false);

    StructColumn* st = down_cast<StructColumn*>(res.get());
    auto fields = st->fields_column();

    size_t chunk_size = columns[0]->size();
    ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
    UnnestedArrayData src_array_data = prepare_array_input(src_column.get());
    DCHECK(src_array_data.elements->is_binary());
    const auto& src_elements =
            down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(*src_array_data.elements).get_data().data();
    const auto& src_offsets = src_array_data.offsets->get_data().data();

    //CELONIS_ALIGN_MODEL gets deduplicated cases with nulls. This means we do not need to perform deduplication in the native operator but we do need to handle nulls
    AlignModelHelper::traces_t deduped_cases;
    deduped_cases.reserve(chunk_size);
    std::vector<int> row_to_case_index;
    row_to_case_index.reserve(chunk_size);

    int case_index = 0;
    for (size_t row = 0; row < chunk_size; row++) {
        if ((src_array_data.null_arrays != nullptr && (*src_array_data.null_arrays)[row])) {
            row_to_case_index.push_back(-1);
            continue;
        }
        AlignModelHelper::trace_t current_case;
        auto start = src_offsets[row];
        auto end = src_offsets[row + 1];
        current_case.reserve(end - start);
        auto non_null_activity_count{0};
        for (size_t i = start; i < end; ++i) {
            if (src_array_data.null_elements != nullptr && (*src_array_data.null_elements)[i]) {
                current_case.emplace_back(std::nullopt);
            } else {
                current_case.emplace_back(src_elements[i].to_string());
                non_null_activity_count++;
            }
        }
        if (non_null_activity_count == 0) {
            row_to_case_index.push_back(-1);
            continue;
        }
        deduped_cases.emplace_back(std::move(current_case));
        row_to_case_index.push_back(case_index++);
    }
    DCHECK_EQ(row_to_case_index.size(), chunk_size);

    AlignModelHelper helper;
    RETURN_IF_ERROR(helper.execute(deduped_cases, json_bpmn_model_description));
    const auto& result_table = helper.result_table();

    const auto& alignment_model_vertex_id =
            result_table.column<std::vector<std::optional<size_t>>>("alignment_model_vertex_id");
    const auto& alignment_vertex_label = result_table.column<std::vector<std::string>>("alignment_vertex_label");
    const auto& alignment_move_type = result_table.column<std::vector<std::string>>("alignment_move_type");
    const auto& alignment_deviation_category = result_table.column<std::vector<std::string>>("alignment_deviation_category");
    const auto& alignment_activity_index = result_table.column<std::vector<row_id>>("alignment_activity_index");
    const auto& association_edge_class = result_table.column<std::vector<row_id>>("association_edge_class");
    const auto& association_alignment_index = result_table.column<std::vector<row_id>>("association_alignment_index");
    const auto& edge_class_id = result_table.column<std::vector<row_id>>("edge_class_id");
    const auto& edge_class_type = result_table.column<std::vector<std::string>>("edge_class_type");

    for (auto index : row_to_case_index) {
        if (index < 0) {
            AddNulls(fields);
            continue;
        }
        AddArray(fields[0], alignment_model_vertex_id[index]);
        AddArray(fields[1], alignment_vertex_label[index]);
        AddArray(fields[2], alignment_move_type[index]);
        AddArray(fields[3], alignment_activity_index[index]);
        AddArray(fields[4], association_edge_class[index]);
        AddArray(fields[5], association_alignment_index[index]);
        AddArray(fields[6], edge_class_id[index]);
        AddArray(fields[7], edge_class_type[index]);
        AddArray(fields[8], alignment_deviation_category[index]);
    }
    return res;
}

} // namespace starrocks