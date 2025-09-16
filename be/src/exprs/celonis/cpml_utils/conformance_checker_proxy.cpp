#include "conformance_checker_proxy.h"

#include <cpml/conformance/deprecated/conformance_checker.h>
#include <cpml/constants.h>
#include <ctl/assert.h>
#include <ctl/concepts.h>
#include <ctl/stop_token.h>
#include <rapidjson/document.h>

#include "column/column_helper.h"
#include "exprs/celonis/util.h"
#include "sr_context.h"
#include "sr_trace_accessor.h"

namespace starrocks::celonis::cpml_utils {

// N.B.: We don't validate the entire consistency of the PN spec here. E.g., we do not check if a marking place is in
// the set of places of that a mapped transition is in the set of transitions (same applies for the arcs). Most of these
// checks are in the petri net class itself (hidden deeper in the CPML conformance callstack).
// TODO(n.weber): The approach will be re-evaluated as part of PMT-1689 which allows to fail early also client-side.
StatusOr<petri_net_description_ptr_t> build_petri_net_description_from_json(const json_petri_net_t& petri_net_as_json) {
    cpml::conformance::deprecated::petri_net_description::builder bldr;

    rapidjson::Document document;
    document.Parse(petri_net_as_json.c_str());
    if (document.HasParseError()) {
        std::stringstream error;
        error << "celonis_conformance: Can't parse JSON specification.";
        return Status::InvalidArgument(error.str());
    }
    { // Parse places
        if (!document.HasMember("places")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'places'.";
            return Status::InvalidArgument(error.str());
        }
        const rapidjson::Value& place_values = document["places"];

        auto places = ctl::make_static_array<std::string>(place_values.Size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(place_values.Begin(), place_values.End(), places.begin(),
                               [](const auto& value) -> std::string { return value.GetString(); });
        bldr = std::move(bldr).places(std::move(places));
    }

    { // Parse transitions
        if (!document.HasMember("transitions")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'transitions'.";
            return Status::InvalidArgument(error.str());
        }
        const rapidjson::Value& transition_values = document["transitions"];

        auto transitions =
                ctl::make_static_array<std::string>(transition_values.Size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(transition_values.Begin(), transition_values.End(), transitions.begin(),
                               [](const auto& value) -> std::string { return value.GetString(); });
        bldr = std::move(bldr).transitions(std::move(transitions));
    }

    using string_pair_type = cpml::conformance::deprecated::petri_net_description::string_pair;
    { // Parse arcs
        if (!document.HasMember("arcs")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'arcs'.";
            return Status::InvalidArgument(error.str());
        }
        const rapidjson::Value& arc_values = document["arcs"];

        auto arcs = ctl::make_static_array<string_pair_type>(arc_values.Size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(arc_values.Begin(), arc_values.End(), arcs.begin(),
                               [](const auto& value) -> string_pair_type {
                                   return {value["from"].GetString(), value["to"].GetString()};
                               });
        bldr = std::move(bldr).arcs(std::move(arcs));
    }

    { // Parse mapping
        if (!document.HasMember("mapping")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'mapping'.";
            return Status::InvalidArgument(error.str());
        }

        const rapidjson::Value& mapping_values = document["mapping"];

        auto mapping =
                ctl::make_static_array<string_pair_type>(mapping_values.Size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(mapping_values.Begin(), mapping_values.End(), mapping.begin(),
                               [](const auto& value) -> string_pair_type {
                                   return {value["from"].GetString(), value["to"].GetString()};
                               });
        bldr = std::move(bldr).mapping(std::move(mapping));
    }

    using marking_type = cpml::conformance::deprecated::petri_net_description::string_int_pair;
    { // Parse initial markings
        if (!document.HasMember("initial_marking")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'initial_marking'.";
            return Status::InvalidArgument(error.str());
        }
        const rapidjson::Value& initial_marking_values = document["initial_marking"];

        auto initial_markings = ctl::make_static_array<marking_type>(initial_marking_values.Size(),
                                                                     ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(initial_marking_values.Begin(), initial_marking_values.End(), initial_markings.begin(),
                               [](const auto& value) -> marking_type {
                                   debug_assert(value["count"].GetInt64() == 1);
                                   return {value["node"].GetString(), 1};
                               });
        bldr = std::move(bldr).initial_markings(std::move(initial_markings));
    }

    { // Parse final markings
        if (!document.HasMember("final_marking")) {
            std::stringstream error;
            error << "celonis_conformance: Your model does not contain 'final_marking'.";
            return Status::InvalidArgument(error.str());
        }
        const rapidjson::Value& final_marking_values = document["final_marking"];

        auto final_markings = ctl::make_static_array<marking_type>(final_marking_values.Size(),
                                                                   ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));

        std::ranges::transform(final_marking_values.Begin(), final_marking_values.End(), final_markings.begin(),
                               [](const auto& value) -> marking_type {
                                   debug_assert(value["count"].GetInt64() == 1);
                                   return {value["node"].GetString(), 1};
                               });
        bldr = std::move(bldr).final_markings(std::move(final_markings));
    }

    return std::make_unique<petri_net_description>(std::move(bldr).build());
}

namespace {

template <typename T>
concept conformance_or_readable_result_type = ctl::one_of<T, cpml::conformance::deprecated::violation_key_t,
                                                          cpml::conformance::deprecated::readable_conformance_result_t>;

/**
 * @brief Transforms the CPML CONFORMANCE/READABLE array output to a SR column array output.
 *
 * The offsets in the output column are copied from the original input array column. We don't need to set the
 * null_arrays/null_elements because the CONFORMANCE/READABLE algorithms don't produce NULLs.
 */
template <conformance_or_readable_result_type T>
[[nodiscard]] ColumnPtr make_output_column(const ctl::array_view<const T> output_data,
                                           const UnnestedArrayData& original_input_array_column) {
    static constexpr bool IS_CONFORMANCE_DATA = std::is_same_v<T, cpml::conformance::deprecated::violation_key_t>;
    using output_column_type =
            std::conditional_t<IS_CONFORMANCE_DATA, RunTimeColumnType<TYPE_BIGINT>, RunTimeColumnType<TYPE_VARCHAR>>;
    /* Transform the CPML output to a SR column output */
    auto result_array_column = ArrayColumn::create(
            NullableColumn::create(output_column_type::create(), NullColumn::create()),
            ColumnHelper::as_column<UInt32Column>(original_input_array_column.offsets->clone()));

    // Copy each conformance result value to the output column elements
    auto& result_elements = result_array_column->elements_column();
    std::ranges::for_each(output_data, [&result_elements](const conformance_or_readable_result_type auto& data) {
        if constexpr (IS_CONFORMANCE_DATA) {
            result_elements->append_datum(data); // conformance result
        } else {
            result_elements->append_datum(Slice{data}); // readable result
        }
    });

    return result_array_column;
}

} // anonymous namespace

conformance_violation_result_column_t check_conformance(
        const conformance_input_column_t& activity_array_data,
        const cpml::conformance::deprecated::petri_net_description& petri_net_description) {
    const auto array_column =
            ColumnHelper::unpack_and_duplicate_const_column(activity_array_data->size(), activity_array_data);
    /* Prepare CPML call input */
    const auto array_data = prepare_array_input(array_column.get());
    const sr_trace_accessor trace_accessor(array_data);
    const ctl::stop_token stoken;
    const auto function_ctx = make_sr_function_context();

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    /* Produce the conformance result output */
    const auto conformance_result =
            cpml::conformance::deprecated::check_conformance(trace_accessor, petri_net_description, stoken, function_ctx);
#pragma GCC diagnostic pop

    return make_output_column(ctl::array_view(conformance_result), array_data);
}

namespace {

// TODO(n.weber): Maybe we could make this transformation redundant in a CPML fopllow-up if we simply accept some
//   integer column data view. Is it possible to get a consecutive int* + size from the column input?
[[nodiscard]] cpml::conformance::deprecated::conformance_result_t from_input_column(
        const conformance_violation_result_column_t& conformance_result) {
    const auto array_column =
            ColumnHelper::unpack_and_duplicate_const_column(conformance_result->size(), conformance_result);
    const auto array_data = prepare_array_input(array_column.get());
    // N.B.: Conformance result cannot contain NULL values. If needed, we could relax this limitation and simply map
    // NULL to conforming.
    // TODO(n.weber): It would be possible for users to explicitly remap a CONFORMANCE result value to NULL. Do we need
    //   to support this? Should this be an error?
    DCHECK(array_data.null_arrays == nullptr);
    DCHECK(array_data.null_elements == nullptr);

    const auto& elements_column = *down_cast<const RunTimeColumnType<TYPE_BIGINT>*>(array_data.elements);
    const auto& elements_column_data = elements_column.get_data();
    auto result = ctl::make_static_array_for_overwrite<cpml::conformance::deprecated::violation_key_t>(
            elements_column_data.size(), ALLOC_MSG(ctl::RETURN_VALUE_MSG));

    for (size_t element_idx = 0; element_idx < result.size(); ++element_idx) {
        result[element_idx] = elements_column_data[element_idx];
    }

    return result;
}

} // namespace

readable_conformance_results_t conformance_result_to_readable_diagnostics(
        const conformance_violation_result_column_t& conformance_result,
        const conformance_input_column_t& activity_array_data) {
    const auto array_column =
            ColumnHelper::unpack_and_duplicate_const_column(activity_array_data->size(), activity_array_data);
    /* Prepare CPML call input */
    const auto array_data = prepare_array_input(array_column.get());
    const auto conformance_output = from_input_column(conformance_result);
    const sr_trace_accessor trace_accessor(array_data);

    /* Produce the readable result output */
    const auto readable_conformance_result =
            cpml::conformance::deprecated::readable_conformance_result(conformance_output, trace_accessor);

    return make_output_column(ctl::array_view(readable_conformance_result), array_data);
}

} // namespace starrocks::celonis::cpml_utils
