#include "create_align_model_tables.h"

#include <glog/logging.h>
#include <string>
#include <string_view>

#include <cpml/model/bpmn_graph.h>
#include <ctl/assert.h>
#include <ctl/scope_guards.h>
#include <ctl/time.h>
#include <format/json/json_fwd.h>

#include "align_model_statistics.h"
#include "deviation_category.h"
#include "modules/common/call_and_log_unsafe_callable.h"
#include "modules/common/execution_context.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/column.h"
#include "modules/memory/table_group.h"
#include "modules/memory/typed_dictionary.h"
#include "modules/operators/aggregation/string_aggregation.h"
#include "modules/operators/process/align_model/align_model_table_group_node_settings.h"
#include "modules/operators/process/align_model/align_model_types.h"
#include "modules/operators/process/align_model/deviation_category.h"
#include "modules/operators/process/align_model/replay_aligned_variant.h"
#include "modules/operators/process/align_model/v1/sr_specific_inflation_glue_code.h"
#include "modules/operators/process/align_model/v2/create_alignment_inflation.h"
#include "modules/query/operators.pb.h"
#include "utils/json_to_model.h"

namespace celonis::accelerator::operators::process::align_model {
namespace {
constexpr size_t CREATE_TABLE_GRAIN_SIZE{100'000};

format::json::json_object_t merge_message_and_details(const std::string& message,
                                                      const format::json::json_object_t& details = {}) {
  format::json::json_object_t json{details};
  static constexpr const char* DATADOG_LOG_MESSAGE_ATTRIBUTE{"message"};
  json[DATADOG_LOG_MESSAGE_ATTRIBUTE] = message;
  return json;
}

std::string remove_enclosing_braces(const std::string& json_formatted_input) {
  debug_assert(std::size(json_formatted_input) >= 2);
  return json_formatted_input.substr(1, std::size(json_formatted_input) - 2);
}

std::string to_json_string_without_enclosing_braces(const format::json::json_object_t& json_input) {
  return remove_enclosing_braces(format::json::to_string(json_input));
}

}  // namespace

memory::table_group_t create_align_model_tables::operator()(const common::execution_context& context) {
  // #lizard forgives
  const auto num_rows_eventlog{activity_column_->get_row_count()};
  align_model_statistics stats{.num_rows_eventlog = num_rows_eventlog};
  auto raii_logger{ctl::finally{[&stats]() noexcept {
    common::call_and_log_unsafe_callable(
        [&]() {
          LOG(INFO) << to_json_string_without_enclosing_braces(
              merge_message_and_details("ALIGN_MODEL computation statistics.", to_json(stats)));
        },
        "Encountered error during ALIGN_MODEL logging.");
  }}};
  auto align_model_op_context{context.create_sub_context("create_align_model_tables::operator()", {})};

  ctl::wall_timer_t proto_bpmn_model_description_to_bpmn_graph_timer{};

  starrocks::celonis::details::dictionary dict{};
  debug_assert(activity_column_->is_cel_string_type());
  const auto dictionary_saola = activity_column_->get_dict(context);
  const auto* ptr_dict = dynamic_cast<memory::typed_dictionary<cel_string_t>*>(dictionary_saola.get());
  debug_assert(dictionary_saola.get());
  for (size_t i{1}; i < dictionary_saola->get_size(); i++) {
    dict.insert(ptr_dict->get_string_value_view(i));
  }

  const auto [bpmn_graph, bpmn_to_string]{
      starrocks::celonis::details::convert_from_proto_and_create_string_map(model_description_, dict)};
  stats.proto_bpmn_to_bpmn_graph = proto_bpmn_model_description_to_bpmn_graph_timer.elapsed_wall_time_so_far();
  stats.bpmn_graph = bpmn_graph;

  ctl::wall_timer_t variants_computation_timer{};
  const auto variants{aggregation::compute_variant_row_ids({.table_one_side_size = case_table_row_count_,
                                                            .column_n_side = activity_column_,
                                                            .projection = activity_to_case_join_},
                                                           context)};
  stats.variant_computation = variants_computation_timer.elapsed_wall_time_so_far();
  stats.variant_count = variants->get_num_traces();

  // Threshold used to determine if the align_model input meta-data should be logged
  static constexpr int VARIANT_COUNT_LOG_THRESHOLD{10'000};
  if (const auto number_of_variants{variants->get_num_traces()}; VARIANT_COUNT_LOG_THRESHOLD <= number_of_variants) {
    log::jinfo("Large ALIGN_MODEL input.",
               {{"number_of_variants", number_of_variants},
                {"event_log_table_size", num_rows_eventlog},
                {"case_table_size", case_table_row_count_},
                {"distinct_events_count", activity_column_->get_domain_count(context, no_dictify_request{})}});
  }

  auto config{align_model_config::make("CACHE_KEY_PRUNED_VARIANTS", settings_.get_alignment_execution_strategy())};

  // per variant alignments and replay results
  const auto [alignments, parallel_vertices]{align_model(variants, bpmn_graph, config, stats,
                                                         activity_column_->optional_table_config().value().table_name(),
                                                         align_model_op_context)};

  // We check whether the alignment table already exceeds the row limit to avoid unnecessarily replaying
  common::runtime_assert(variants->get_case_to_trace_col_ptrs().has_value(), "ALIGN_MODEL: Missing case->variant map");

  ctl::wall_timer_t replay_timer{};
  const auto replay_results{replay_aligned_variants(bpmn_graph, alignments, parallel_vertices, align_model_op_context)};
  stats.time_variant_replay = replay_timer.elapsed_wall_time_so_far();

  memory::table_group_t tables{};
  if (settings_.get_version() == align_model_version::V1) {
    deviation_categories_for_cases_t deviation_categories{};
    if (output_projection_.needs_deviation_categories()) {
      ctl::wall_timer_t deviation_category_timer{};
      deviation_categories = compute_categories<compute_incomplete_category::NO>(alignments);
      stats.time_deviation_categories = deviation_category_timer.elapsed_wall_time_so_far();
    }
    tables = v1::create_tables(                  //
        alignments,                              //
        replay_results,                          //
        deviation_categories,                    //
        bpmn_to_string,                          //
        variants,                                //
        activity_column_,                        //
        case_column_,                            //
        activity_to_case_join_,                  //
        align_model_op_context,                  //
        settings_.get_create_table_grain_size()  //
    );
  } else if (settings_.get_version() == align_model_version::V2) {
    deviation_categories_for_cases_t deviation_categories{};
    if (output_projection_.needs_deviation_categories()) {
      ctl::wall_timer_t deviation_category_timer{};
      deviation_categories = compute_categories<compute_incomplete_category::NO>(alignments);
      stats.time_deviation_categories = deviation_category_timer.elapsed_wall_time_so_far();
    }
    tables = v2::create_tables<compute_incomplete_category::NO>(  //
        alignments,                                               //
        replay_results,                                           //
        deviation_categories,                                     //
        bpmn_to_string,                                           //
        variants,                                                 //
        activity_column_,                                         //
        case_column_,                                             //
        activity_to_case_join_,                                   //
        align_model_op_context,                                   //
        settings_.get_create_table_grain_size(),                  //
        output_projection_                                        //
    );
  } else {
    debug_assert(settings_.get_version() == align_model_version::V3);
    deviation_categories_for_cases_t deviation_categories{};
    if (output_projection_.needs_deviation_categories()) {
      ctl::wall_timer_t deviation_category_timer{};
      deviation_categories = compute_categories<compute_incomplete_category::YES>(alignments);
      stats.time_deviation_categories = deviation_category_timer.elapsed_wall_time_so_far();
    }
    tables = v2::create_tables<compute_incomplete_category::YES>(  //
        alignments,                                                //
        replay_results,                                            //
        deviation_categories,                                      //
        bpmn_to_string,                                            //
        variants,                                                  //
        activity_column_,                                          //
        case_column_,                                              //
        activity_to_case_join_,                                    //
        align_model_op_context,                                    //
        settings_.get_create_table_grain_size(),
        output_projection_  //
    );
  }

  stats.status = computation_status::SUCCESS;
  return tables;
}

}  // namespace celonis::accelerator::operators::process::align_model
