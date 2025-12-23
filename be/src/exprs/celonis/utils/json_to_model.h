#pragma once

#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph.h>

#include <boost/multi_index/ranked_index.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>

#include "column/column.h"
#include "column/datum.h"
#include "exprs/celonis/utils/proto_utils.h"

namespace starrocks::celonis {

namespace details {

using row_id = int64_t;

using dictionary = boost::multi_index::multi_index_container<
        std::string_view, boost::multi_index::indexed_by<boost::multi_index::ranked_unique<
                                  boost::multi_index::identity<std::string_view>, std::less<>>>>;

using bpmn_to_string_t = std::unordered_map<cpml::model::bpmn::vertex_id_type, std::string>;

[[nodiscard]] row_id dictionary_get_row_id_for(const dictionary& dict, const std::string_view task_name);

/** Given a proto description node of a bpmn node and possibly a dictionary. It returns the bpmn node, it uses the dictionary
 * for the id number or if that does not exist, uses hashing.*/
[[nodiscard]] cpml::model::bpmn::vertex_type proto_node_to_vertex(
        const bpmn_model_description::bpmn_node& proto_bpmn_node, const dictionary* dictionary);

/** This function exist to replace the current modules Saola code. It does almost the same as transform_to_bpmn_graph but uses
 * a dictionary instead of hashes and returns a bpmn_to_string_t variable.
 */
std::pair<cpml::model::bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
        const bpmn_model_description& bpmn_proto, const dictionary& dict);

/** Given the json model of a bpmn graph, it gives back the proto description of it.*/
[[nodiscard]] StatusOr<bpmn_model_description> transform_to_proto_bpmn(const nlohmann::json& json_model);

/** Given the proto_description of a bpmn graph, it gives back the bpmn_graph. Uses hashing for the activity ids. */
[[nodiscard]] cpml::model::bpmn_graph transform_to_bpmn_graph(const bpmn_model_description& proto_description);

} // namespace details

/** Given a json_model of a bpmn graph, it gives back the bpmn_graph. Uses hashing for the activity ids. */
[[nodiscard]] StatusOr<cpml::model::bpmn_graph> transform_to_bpmn_graph(const nlohmann::json& json_model);

} // namespace starrocks::celonis