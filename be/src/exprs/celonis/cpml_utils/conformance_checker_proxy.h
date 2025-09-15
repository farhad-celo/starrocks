#pragma once

#include <cpml/conformance/deprecated/petri_net_description.h>

#include <memory>
#include <string>

#include "column/column.h"
#include "common/statusor.h"

namespace starrocks::celonis::cpml_utils {

using json_petri_net_t = std::string;
using petri_net_description = cpml::conformance::deprecated::petri_net_description;
using petri_net_description_ptr_t = std::unique_ptr<petri_net_description>;

[[nodiscard]] StatusOr<petri_net_description_ptr_t> build_petri_net_description_from_json(
        const json_petri_net_t& petri_net_as_json);

// The activities array
using conformance_input_column_t = ColumnPtr;
// The conformance result column containing the integer violation values
using conformance_violation_result_column_t = ColumnPtr;

[[nodiscard]] conformance_violation_result_column_t check_conformance(
        const conformance_input_column_t& activity_array_data,
        const cpml::conformance::deprecated::petri_net_description& petri_net_description);

// The readable conformance result column containing the string diagnostic values
using readable_conformance_results_t = ColumnPtr;

[[nodiscard]] readable_conformance_results_t conformance_result_to_readable_diagnostics(
        const conformance_violation_result_column_t& conformance_result_column,
        const conformance_input_column_t& activity_array_data);

} // namespace starrocks::celonis::cpml_utils
