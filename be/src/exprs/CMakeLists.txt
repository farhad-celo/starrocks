# Copyright 2021-present StarRocks, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(LIBRARY_OUTPUT_PATH "${BUILD_DIR}/src/exprs")

# where to put generated binaries
set(EXECUTABLE_OUTPUT_PATH "${BUILD_DIR}/src/exprs")

set(EXPR_FILES
  agg/data_sketch/ds_theta.cpp
  agg/factory/aggregate_factory.cpp
  agg/factory/aggregate_resolver_approx.cpp
  agg/factory/aggregate_resolver_avg.cpp
  agg/factory/aggregate_resolver_hypothesis_testing.cpp
  agg/factory/aggregate_resolver_minmaxany.cpp
  agg/factory/aggregate_resolver_others.cpp
  agg/factory/aggregate_resolver_sumcount.cpp
  agg/factory/aggregate_resolver_stream.cpp
  agg/factory/aggregate_resolver_utility.cpp
  agg/factory/aggregate_resolver_variance.cpp
  agg/factory/aggregate_resolver_window.cpp
  base64.cpp
  binary_functions.cpp
  expr_context.cpp
  expr.cpp
  function_context.cpp
  table_function/table_function_factory.cpp
  table_function/json_each.cpp
  table_function/list_rowsets.cpp
  arithmetic_expr.cpp
  array_element_expr.cpp
  array_expr.cpp
  array_functions.cpp
  arrow_function_call.cpp
  binary_predicate.cpp
  bitmap_functions.cpp
  case_expr.cpp
  cast_expr.cpp
  cast_expr_array.cpp
  cast_expr_json.cpp
  cast_expr_struct.cpp
  cast_expr_map.cpp
  cast_nested.cpp
  column_ref.cpp
  placeholder_ref.cpp
  dictmapping_expr.cpp
  dict_functions.cpp
  compound_predicate.cpp
  condition_expr.cpp
  encryption_functions.cpp
  es_functions.cpp
  find_in_set.cpp
  function_call_expr.cpp
  function_helper.cpp
  geo_functions.cpp
  grouping_sets_functions.cpp
  hyperloglog_functions.cpp
  in_const_predicate.cpp
  info_func.cpp
  inet_aton.cpp
  in_predicate.cpp
  is_null_predicate.cpp
  json_functions.cpp
  jsonpath.cpp
  like_predicate.cpp
  literal.cpp
  locate.cpp
  map_element_expr.cpp
  map_functions.cpp
  struct_functions.cpp
  math_functions.cpp
  percentile_functions.cpp
  runtime_filter_bank.cpp
  runtime_filter.cpp
  split.cpp
  regexp_split.cpp
  split_part.cpp
  str_to_map.cpp
  string_functions.cpp
  time_functions.cpp
  utility_functions.cpp
  agg/java_udaf_function.cpp
  agg/java_window_function.cpp
  table_function/java_udtf_function.cpp
  java_function_call_expr.cpp
  lambda_function.cpp
  array_map_expr.cpp
  subfield_expr.cpp
  map_apply_expr.cpp
  map_expr.cpp
  substring_index.cpp
  dict_query_expr.cpp
  runtime_filter_layout.cpp
  dictionary_get_expr.cpp
  ngram.cpp
  match_expr.cpp
)

if(STARROCKS_JIT_ENABLE)
  set(EXPR_FILES ${EXPR_FILES}
      jit/ir_helper.cpp
      jit/jit_engine.cpp
      jit/jit_expr.cpp)

endif()

set(CELONIS_FILES
  celonis/agg/cluster_strings.cpp
  celonis/agg/cluster_variants.cpp
  celonis/agg/enumerate_node_paths.cpp
  celonis/agg/weekday_calendar.cpp
  celonis/agg/factory_calendar.cpp
  celonis/agg/workday_calendar.cpp
  celonis/agg/inductive_miner.cpp
  celonis/agg/kmeans.cpp
  celonis/agg/linear_regression.cpp
  celonis/agg/mode.cpp
  celonis/agg/product.cpp
  celonis/agg/variant_util.cpp
  celonis/agg/util.cpp
  celonis/agg/variant_agg.cpp
  celonis/agg/variant_stats.cpp
  celonis/agg/variant_stats_v2.cpp
  celonis/abc_model.cpp
  celonis/adjust_daily_timestamps.cpp
  celonis/align_model.cpp
  celonis/array_avg.cpp
  celonis/array_count_distinct.cpp
  celonis/array_end_finder.cpp
  celonis/array_functions.cpp
  celonis/calc_throughput.cpp
  celonis/calculate_range_end.cpp
  celonis/conformance.cpp
  celonis/decode_string.cpp
  celonis/encode_string.cpp
  celonis/encode_variant.cpp
  celonis/greatest_least.cpp
  celonis/in.cpp
  celonis/index_activity.cpp
  celonis/kmeans.cpp
  celonis/like.cpp
  celonis/linear_regression.cpp
  celonis/match_activities.cpp
  celonis/match_pattern_util.cpp
  celonis/match_process.cpp
  celonis/math_functions.cpp
  celonis/mo_bpmn_graph.cpp
  celonis/multi_in.cpp
  celonis/patindex.cpp
  celonis/peek_merged_sorted_arrays.cpp
  celonis/qnorm.cpp
  celonis/remap_timestamp_weekday.cpp
  celonis/remap_values.cpp
  celonis/serialization_utils.cpp
  celonis/shortened_variant.cpp
  celonis/source_target.cpp
  celonis/stringhash.cpp
  celonis/string_functions.cpp
  celonis/table_function/count_edges.cpp
  celonis/time_functions.cpp
  celonis/to_double.cpp
  celonis/transits_interleaved.cpp
  celonis/transits_match.cpp
  celonis/util.cpp
)

add_library(blake2s STATIC IMPORTED)
set_target_properties(blake2s PROPERTIES IMPORTED_LOCATION ${THIRDPARTY_DIR}/lib/libblake2s.a)

add_library(Exprs ${EXPR_FILES} ${CELONIS_FILES})
target_include_directories(Exprs PRIVATE "${BUILD_DIR}/generated" "celonis/cpml_proxy/include")
target_link_libraries(Exprs PRIVATE blake2s)
target_link_libraries(Exprs PRIVATE celonis_protos)
