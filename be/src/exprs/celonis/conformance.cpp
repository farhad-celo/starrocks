#include "exprs/celonis/conformance.h"

#include <memory>

#include "exprs/celonis/cpml_utils/conformance_checker_proxy.h"

namespace starrocks {

struct ConformanceState {
    celonis::cpml_utils::petri_net_description_ptr_t petri_net;
};

Status CelonisConformance::conformance_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    if (context->get_num_constant_columns() != 2) {
        return Status::InvalidArgument("celonis_conformance needs 2 parameters: column, json spec");
    }
    if (!context->is_notnull_constant_column(1)) {
        return Status::InvalidArgument("celonis_conformance only supports constant json spec");
    }

    const auto json_input = context->get_constant_column(1);
    std::string json = ColumnHelper::get_const_value<TYPE_VARCHAR>(json_input).to_string();

    auto* state = new ConformanceState();
    context->set_function_state(scope, state);
    ASSIGN_OR_RETURN(state->petri_net, celonis::cpml_utils::build_petri_net_description_from_json(json));

    return Status::OK();
}

Status CelonisConformance::conformance_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<ConformanceState*>(context->get_function_state(scope));
        delete state;
    }

    return Status::OK();
}

StatusOr<ColumnPtr> CelonisConformance::conformance(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});

    const auto* state =
            reinterpret_cast<const ConformanceState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    return celonis::cpml_utils::check_conformance(columns[0], *state->petri_net);
}

StatusOr<ColumnPtr> CelonisConformance::readable_conformance(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});

    const auto conformance_result{conformance(context, columns)};
    return celonis::cpml_utils::conformance_result_to_readable_diagnostics(conformance_result.value(), columns[0]);
}

StatusOr<ColumnPtr> CelonisReadableConformance::readable_conformance([[maybe_unused]] FunctionContext* context,
                                                                     const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});

    return celonis::cpml_utils::conformance_result_to_readable_diagnostics(columns[0], columns[1]);
}

} // namespace starrocks
