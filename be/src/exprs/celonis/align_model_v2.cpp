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
void AddArray(ColumnPtr& column, const std::vector<std::string>& input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto& entry : input) {
        datum.emplace_back(Slice(entry));
    }
    column->append_datum(datum);
}

void AddArray(ColumnPtr& column, const std::vector<row_id>& input) {
    DatumArray datum;
    datum.reserve(input.size());
    for (const auto& entry : input) {
        datum.emplace_back(static_cast<int64_t>(entry));
    }
    column->append_datum(datum);
}

void AddArray(ColumnPtr& column, const std::vector<std::optional<size_t>>& input) {
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

enum class move_type {
    EXCLUSIVE_VIOLATION,
    LOG_EDGE,
    MISSING_VIOLATION,
    MODEL_EDGE,
    SKIP_EDGE,
    SYNC_EDGE,
    UNMAPPED_EDGE,
    INCOMPLETE_VIOLATION
};
std::string get_move_type(const move_type t) {
    switch (t) {
    case move_type::EXCLUSIVE_VIOLATION:
        return "EXCLUSIVE_VIOLATION";
    case move_type::LOG_EDGE:
        return "LOG_EDGE";
    case move_type::MISSING_VIOLATION:
        return "MISSING_VIOLATION";
    case move_type::MODEL_EDGE:
        return "MODEL_EDGE";
    case move_type::SKIP_EDGE:
        return "SKIP_EDGE";
    case move_type::SYNC_EDGE:
        return "SYNC_EDGE";
    case move_type::UNMAPPED_EDGE:
        return "UNMAPPED_EDGE";
    case move_type::INCOMPLETE_VIOLATION:
        return "INCOMPLETE_VIOLTION";
    }
    __builtin_unreachable();
}
struct alignment_move_columns {
    celonis::ResultColumn<std::vector<row_id>>& alignment_index;
    celonis::ResultColumn<std::vector<std::string>>& deviation_category;
    celonis::ResultColumn<std::vector<row_id>>& edge_class;
    celonis::ResultColumn<std::vector<std::optional<size_t>>>& model_vertex_id;
    celonis::ResultColumn<std::vector<std::string>>& move_type;
    celonis::ResultColumn<std::vector<std::string>>& vertex_label;
};

alignment_move_columns get_alignment_move_columns(const ResultTable& result_table, move_type type) {
    const auto move_type_string{get_move_type(type)};

    return {result_table.column<std::vector<row_id>>(move_type_string + "_alignment_index"),
            result_table.column<std::vector<std::string>>(move_type_string + "_deviation_category"),
            result_table.column<std::vector<row_id>>(move_type_string + "_edge_class"),
            result_table.column<std::vector<std::optional<size_t>>>(move_type_string + "_model_vertex_id"),
            result_table.column<std::vector<std::string>>(move_type_string + "_move_type"),
            result_table.column<std::vector<std::string>>(move_type_string + "_vertex_label")};
}

} // namespace

struct AlignModelStateFragmentLocal {
    std::string json_bpmn_model_description;
};

Status CelonisAlignModelV2::align_model_v2_prepare(FunctionContext* context,
                                                   FunctionContext::FunctionStateScope scope) {
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
    RETURN_IF_ERROR(helper.execute(deduped_cases, json_bpmn_model_description,
                                   AlignModelHelper::celostar_align_model_version::V2));
    const auto& result_table = helper.result_table();

    const auto& variant_column = result_table.column<std::vector<std::string>>("variant");
    const auto& alignment_model_vertex_id =
            result_table.column<std::vector<std::optional<size_t>>>("alignment_model_vertex_id");
    const auto& alignment_vertex_label = result_table.column<std::vector<std::string>>("alignment_vertex_label");

    const auto& alignment_move_type = result_table.column<std::vector<std::string>>("alignment_move_type");
    const auto& alignment_deviation_category =
            result_table.column<std::vector<std::string>>("alignment_deviation_category");
    const auto& alignment_activity_index = result_table.column<std::vector<row_id>>("alignment_activity_index");

    const auto exclusive_violation_columns{get_alignment_move_columns(result_table, move_type::EXCLUSIVE_VIOLATION)};
    const auto log_edge_columns{get_alignment_move_columns(result_table, move_type::LOG_EDGE)};
    const auto missing_violation_columns{get_alignment_move_columns(result_table, move_type::MISSING_VIOLATION)};
    const auto model_edge_columns{get_alignment_move_columns(result_table, move_type::MODEL_EDGE)};
    const auto skip_edge_columns{get_alignment_move_columns(result_table, move_type::SKIP_EDGE)};
    const auto sync_edge_columns{get_alignment_move_columns(result_table, move_type::SYNC_EDGE)};
    const auto unmapped_edge_columns{get_alignment_move_columns(result_table, move_type::UNMAPPED_EDGE)};

    const auto checked_add_array_to_field{
            [&fields](int& fields_index, const alignment_move_columns& columns, int index) {
                AddArray(fields[fields_index++], columns.alignment_index[index]);
                AddArray(fields[fields_index++], columns.deviation_category[index]);
                AddArray(fields[fields_index++], columns.edge_class[index]);
                AddArray(fields[fields_index++], columns.model_vertex_id[index]);
                AddArray(fields[fields_index++], columns.move_type[index]);
                AddArray(fields[fields_index++], columns.vertex_label[index]);
            }};
    for (auto index : row_to_case_index) {
        if (index < 0) {
            AddNulls(fields);
            continue;
        }
        int fields_index{0};

        AddArray(fields[fields_index++], variant_column[index]);
        AddArray(fields[fields_index++], alignment_model_vertex_id[index]);
        AddArray(fields[fields_index++], alignment_vertex_label[index]);
        AddArray(fields[fields_index++], alignment_move_type[index]);
        AddArray(fields[fields_index++], alignment_activity_index[index]);
        AddArray(fields[fields_index++], alignment_deviation_category[index]);
        checked_add_array_to_field(fields_index, exclusive_violation_columns, index);
        checked_add_array_to_field(fields_index, log_edge_columns, index);
        checked_add_array_to_field(fields_index, missing_violation_columns, index);
        checked_add_array_to_field(fields_index, model_edge_columns, index);
        checked_add_array_to_field(fields_index, skip_edge_columns, index);
        checked_add_array_to_field(fields_index, sync_edge_columns, index);
        checked_add_array_to_field(fields_index, unmapped_edge_columns, index);
    }
    return res;
}

} // namespace starrocks
