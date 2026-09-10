#include "exprs/celonis/create_alignment.h"

#include <fmt/format.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/celonis/modules/operators/process/align_model/v2/create_alignment_output_projection.h"
#include "util.h"

using celonis::accelerator::operators::process::align_model::AlignModelHelper;
using celonis::accelerator::operators::process::align_model::v2::create_alignment_output_projection;
using celonis::accelerator::operators::process::align_model::v2::create_alignment_output_value_type;
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

} // namespace

struct CreateAlignmentStateFragmentLocal {
    CreateAlignmentStateFragmentLocal(celonis::bpmn_model_description model,
                                      create_alignment_output_projection projection)
            : bpmn_model_description(std::move(model)), output_projection(std::move(projection)) {}

    const celonis::bpmn_model_description bpmn_model_description;
    const create_alignment_output_projection output_projection;
};

namespace {

StatusOr<std::optional<celonis::bpmn_model_description>> parse_model_argument(FunctionContext* context,
                                                                              std::string_view function_name) {
    if (!context->is_constant_column(1)) {
        return Status::InvalidArgument(
                fmt::format("The second parameter of {}() only accepts a literal value", function_name));
    }
    if (!context->is_notnull_constant_column(1)) {
        return std::nullopt;
    }

    const auto json_bpmn_model_description =
            ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(1)).to_string();
    auto bpmn_model_description = AlignModelHelper::parse_bpmn_model_description(json_bpmn_model_description);
    if (!bpmn_model_description.ok()) {
        return bpmn_model_description.status();
    }
    return std::optional<celonis::bpmn_model_description>{std::move(bpmn_model_description).value()};
}

Status prepare_create_alignment(FunctionContext* context, FunctionContext::FunctionStateScope scope,
                                create_alignment_output_projection output_projection, std::string_view function_name) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    ASSIGN_OR_RETURN(auto bpmn_model_description, parse_model_argument(context, function_name));
    if (!bpmn_model_description.has_value()) {
        return Status::OK();
    }

    auto state = std::make_unique<CreateAlignmentStateFragmentLocal>(std::move(bpmn_model_description).value(),
                                                                     std::move(output_projection));
    context->set_function_state(scope, state.get());
    state.release();
    return Status::OK();
}

} // namespace

Status CelonisCreateAlignment::create_alignment_prepare(FunctionContext* context,
                                                        FunctionContext::FunctionStateScope scope) {
    return prepare_create_alignment(context, scope, create_alignment_output_projection::all_fields_v1(),
                                    "celonis_create_alignment");
}

Status CelonisCreateAlignment::create_alignment_v2_prepare(FunctionContext* context,
                                                           FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    if (context->get_num_args() == 2) {
        ASSIGN_OR_RETURN(auto output_projection,
                         create_alignment_output_projection::from_mask_and_return_fields(
                                 static_cast<std::int64_t>(create_alignment_output_projection::ALL_FIELDS_MASK),
                                 context->get_return_type().field_names));
        return prepare_create_alignment(context, scope, std::move(output_projection), "celonis_create_alignment_v2");
    }
    if (context->get_num_args() != 3) {
        return Status::InvalidArgument("celonis_create_alignment_v2() requires two or three parameters");
    }
    const auto* mask_type = context->get_arg_type(2);
    if (mask_type == nullptr || mask_type->type != TYPE_BIGINT) {
        return Status::InvalidArgument(
                "The third parameter required_fields_mask of celonis_create_alignment_v2() must have type BIGINT");
    }
    if (!context->is_constant_column(2)) {
        return Status::InvalidArgument(
                "The third parameter required_fields_mask of celonis_create_alignment_v2() must be constant");
    }
    if (!context->is_notnull_constant_column(2)) {
        return Status::InvalidArgument(
                "The third parameter required_fields_mask of celonis_create_alignment_v2() must not be NULL");
    }

    const auto mask = ColumnHelper::get_const_value<TYPE_BIGINT>(context->get_constant_column(2));
    ASSIGN_OR_RETURN(auto output_projection, create_alignment_output_projection::from_mask_and_return_fields(
                                                     mask, context->get_return_type().field_names));
    return prepare_create_alignment(context, scope, std::move(output_projection), "celonis_create_alignment_v2");
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

namespace {

using model_vertex_id_column = std::vector<std::optional<size_t>>;
using string_column = std::vector<std::string>;
using row_id_column = std::vector<row_id>;
using result_column_source =
        std::variant<const celonis::ResultColumn<model_vertex_id_column>*, const celonis::ResultColumn<string_column>*,
                     const celonis::ResultColumn<row_id_column>*>;

enum class create_alignment_version : bool { V1, V2 };

template <create_alignment_version CREATE_ALIGNMENT_VERSION>
StatusOr<ColumnPtr> create_alignment_impl(FunctionContext* context, const Columns& columns,
                                          std::string_view function_name) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto* align_model_state_fragment_local = reinterpret_cast<const CreateAlignmentStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    const auto& bpmn_model_description = align_model_state_fragment_local->bpmn_model_description;
    const auto& output_projection = align_model_state_fragment_local->output_projection;

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
    if constexpr (CREATE_ALIGNMENT_VERSION == create_alignment_version::V2) {
        RETURN_IF_ERROR(helper.execute(deduped_cases, bpmn_model_description,
                                       AlignModelHelper::celostar_align_model_version::V3, output_projection));
    } else {
        RETURN_IF_ERROR(helper.execute(deduped_cases, bpmn_model_description,
                                       AlignModelHelper::celostar_align_model_version::V2, output_projection));
    }
    const auto& result_table = helper.result_table();

    // TODO(m.dierschke) Consider writing directly into starrocks columns instead of using an intermediate format.
    // TODO(m.dierschke) If an intermediate format is needed, consider using CRTP instead of hardcoded polymorphism.
    std::vector<result_column_source> result_column_sources;
    result_column_sources.reserve(st->field_names().size());
    for (const auto& field_name : st->field_names()) {
        const auto* descriptor = create_alignment_output_projection::find_field(field_name);
        if (descriptor == nullptr) {
            return Status::InternalError(fmt::format("{}: Unknown return field '{}'.", function_name, field_name));
        }
        if (!result_table.columns().contains(field_name)) {
            return Status::InternalError(
                    fmt::format("{}: Result field '{}' was not materialized.", function_name, field_name));
        }
        switch (descriptor->value_type) {
        case create_alignment_output_value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY:
            result_column_sources.emplace_back(&result_table.column<model_vertex_id_column>(field_name));
            break;
        case create_alignment_output_value_type::STRING_ARRAY:
            result_column_sources.emplace_back(&result_table.column<string_column>(field_name));
            break;
        case create_alignment_output_value_type::ROW_ID_ARRAY:
            result_column_sources.emplace_back(&result_table.column<row_id_column>(field_name));
            break;
        }
    }
    for (auto index : row_to_case_index) {
        if (index < 0) {
            AddNulls(fields);
            continue;
        }

        for (size_t field_index = 0; field_index < result_column_sources.size(); ++field_index) {
            std::visit([&](const auto* source_column) { AddArray(fields[field_index], source_column->at(index)); },
                       result_column_sources[field_index]);
        }
    }

    return res;
}
template StatusOr<ColumnPtr> create_alignment_impl<create_alignment_version::V1>(FunctionContext* context,
                                                                                 const Columns& columns,
                                                                                 std::string_view function_name);
template StatusOr<ColumnPtr> create_alignment_impl<create_alignment_version::V2>(FunctionContext* context,
                                                                                 const Columns& columns,
                                                                                 std::string_view function_name);

} // namespace

StatusOr<ColumnPtr> CelonisCreateAlignment::create_alignment(FunctionContext* context, const Columns& columns) {
    return create_alignment_impl<create_alignment_version::V1>(context, columns, "celonis_create_alignment");
}

StatusOr<ColumnPtr> CelonisCreateAlignment::create_alignment_v2(FunctionContext* context, const Columns& columns) {
    return create_alignment_impl<create_alignment_version::V2>(context, columns, "celonis_create_alignment_v2");
}

} // namespace starrocks
