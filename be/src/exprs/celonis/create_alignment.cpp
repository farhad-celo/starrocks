#include "exprs/celonis/create_alignment.h"

#include <fmt/format.h>

#include <optional>
#include <vector>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_types_proxy.h"
#include "modules/operators/process/align_model/align_model_types_proxy.h"
#include "util.h"

using celonis::accelerator::operators::process::align_model::AlignModelHelper;
using celonis::accelerator::operators::process::align_model::cs_edge_type_to_string_v2;
using celonis::accelerator::operators::process::align_model::CS_EDGE_TYPES;
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

std::unordered_map<std::string, int> string_to_idx_map(const std::vector<std::string>& vec) {
    std::unordered_map<std::string, int> result;

    for (size_t i = 0; i < vec.size(); ++i) {
        result.emplace(vec[i], i);
    }

    return result;
}

} // namespace

struct CreateAlignmentStateFragmentLocal {
    const celonis::bpmn_model_description bpmn_model_description;
};

Status CelonisCreateAlignment::create_alignment_prepare(FunctionContext* context,
                                                        FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::InvalidArgument(
                    "The second parameter of celonis_create_alignment() only accepts a literal value");
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        // As of 2023-10-11, get_const_value() is not thread-safe. So it shouldn't be called in align_model().
        const auto json_bpmn_model_description =
                ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(1)).to_string();
        auto bpmn_model_description = AlignModelHelper::parse_bpmn_model_description(json_bpmn_model_description);
        if (!bpmn_model_description.ok()) {
            return bpmn_model_description.status();
        }
        auto state = new CreateAlignmentStateFragmentLocal{std::move(bpmn_model_description).value()};
        context->set_function_state(scope, state);
    }

    return Status::OK();
}

Status CelonisCreateAlignment::create_alignment_close(FunctionContext* context,
                                                      FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* align_model_state_fragment_local = reinterpret_cast<const CreateAlignmentStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete align_model_state_fragment_local;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisCreateAlignment::create_alignment(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto* align_model_state_fragment_local = reinterpret_cast<const CreateAlignmentStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    const auto& bpmn_model_description = align_model_state_fragment_local->bpmn_model_description;

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

    //CELONIS_CREATE_ALIGNMENT gets deduplicated cases with nulls. This means we do not need to perform deduplication in the native operator but we do need to handle nulls.
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
    RETURN_IF_ERROR(
            helper.execute(deduped_cases, bpmn_model_description, AlignModelHelper::celostar_align_model_version::V2));
    const auto& result_table = helper.result_table();

    // TODO(m.dierschke): Refactor this code for clarity and maintainability.
    // TODO(m.dierschke) Consider writing directly into starrocks columns instead of using an intermediate format.
    // TODO(m.dierschke) If an intermediate format is needed, consider using CRTP instead of hardcoded polymorphism.
    using model_vertex_id_column = std::vector<std::optional<size_t>>;
    using vertex_label_column = std::vector<std::string>;
    using move_type_column = std::vector<std::string>;
    using deviation_category_column = std::vector<std::string>;
    using activity_index_column = std::vector<row_id>;
    using edge_class_column = std::vector<row_id>;

    constexpr std::string_view MODEL_VERTEX_ID_SUFFIX{"model_vertex_id"};
    constexpr std::string_view VERTEX_LABEL_SUFFIX{"vertex_label"};
    constexpr std::string_view MOVE_TYPE_SUFFIX{"move_type"};
    constexpr std::string_view DEVIATION_CATEGORY_SUFFIX{"deviation_category"};
    constexpr std::string_view ACTIVITY_INDEX_SUFFIX{"activity_index"};
    constexpr std::string_view EDGE_CLASS_SUFFIX{"edge_class"};
    constexpr std::string_view ALIGNMENT_INDEX_SUFFIX{"alignment_index"};
    constexpr std::string_view ALIGNMENT_PREFIX{"alignment"};

    const std::string alignment_model_vertex_id_name{fmt::format("{}_{}", ALIGNMENT_PREFIX, MODEL_VERTEX_ID_SUFFIX)};
    const std::string alignment_vertex_label_name{fmt::format("{}_{}", ALIGNMENT_PREFIX, VERTEX_LABEL_SUFFIX)};
    const std::string alignment_move_type_name{fmt::format("{}_{}", ALIGNMENT_PREFIX, MOVE_TYPE_SUFFIX)};
    const std::string alignment_deviation_category_name{
            fmt::format("{}_{}", ALIGNMENT_PREFIX, DEVIATION_CATEGORY_SUFFIX)};
    const std::string alignment_activity_index_name{fmt::format("{}_{}", ALIGNMENT_PREFIX, ACTIVITY_INDEX_SUFFIX)};

    const auto& alignment_model_vertex_id{result_table.column<model_vertex_id_column>(alignment_model_vertex_id_name)};
    const auto& alignment_vertex_label{result_table.column<vertex_label_column>(alignment_vertex_label_name)};
    const auto& alignment_move_type{result_table.column<move_type_column>(alignment_move_type_name)};
    const auto& alignment_deviation_category{
            result_table.column<deviation_category_column>(alignment_deviation_category_name)};
    const auto& alignment_activity_index{result_table.column<activity_index_column>(alignment_activity_index_name)};

    auto map{string_to_idx_map(st->field_names())};

    for (auto index : row_to_case_index) {
        if (index < 0) {
            AddNulls(fields);
            continue;
        }

        //Alignment table
        AddArray(fields[map.at(alignment_model_vertex_id_name)], alignment_model_vertex_id[index]);
        AddArray(fields[map.at(alignment_vertex_label_name)], alignment_vertex_label[index]);
        AddArray(fields[map.at(alignment_move_type_name)], alignment_move_type[index]);
        AddArray(fields[map.at(alignment_activity_index_name)], alignment_activity_index[index]);
        AddArray(fields[map.at(alignment_deviation_category_name)], alignment_deviation_category[index]);

        // Edge tables
        for (auto type : CS_EDGE_TYPES) {
            const std::string_view edge_type_str{cs_edge_type_to_string_v2(type)};
            const std::string edge_class_name{fmt::format("{}_{}", edge_type_str, EDGE_CLASS_SUFFIX)};
            const std::string model_vertex_id_name{fmt::format("{}_{}", edge_type_str, MODEL_VERTEX_ID_SUFFIX)};
            const std::string vertex_label_name{fmt::format("{}_{}", edge_type_str, VERTEX_LABEL_SUFFIX)};
            const std::string move_type_name{fmt::format("{}_{}", edge_type_str, MOVE_TYPE_SUFFIX)};
            const std::string deviation_category_name{fmt::format("{}_{}", edge_type_str, DEVIATION_CATEGORY_SUFFIX)};
            const std::string alignment_index_name{fmt::format("{}_{}", edge_type_str, ALIGNMENT_INDEX_SUFFIX)};

            const auto& edge_class = result_table.column<edge_class_column>(edge_class_name);
            const auto& model_vertex_id = result_table.column<model_vertex_id_column>(model_vertex_id_name);
            const auto& vertex_label = result_table.column<vertex_label_column>(vertex_label_name);
            const auto& move_type = result_table.column<move_type_column>(move_type_name);
            const auto& deviation_category = result_table.column<deviation_category_column>(deviation_category_name);
            const auto& alignment_index = result_table.column<activity_index_column>(alignment_index_name);

            AddArray(fields[map.at(edge_class_name)], edge_class[index]);
            AddArray(fields[map.at(model_vertex_id_name)], model_vertex_id[index]);
            AddArray(fields[map.at(vertex_label_name)], vertex_label[index]);
            AddArray(fields[map.at(move_type_name)], move_type[index]);
            AddArray(fields[map.at(deviation_category_name)], deviation_category[index]);
            AddArray(fields[map.at(alignment_index_name)], alignment_index[index]);
        }
    }

    return res;
}

} // namespace starrocks
