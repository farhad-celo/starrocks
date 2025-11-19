// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/any_value.h"
#include "exprs/agg/factory/aggregate_factory.hpp"
#include "exprs/agg/factory/aggregate_resolver.hpp"
#include "exprs/agg/group_concat.h"
#include "exprs/agg/percentile_cont.h"
#include "types/logical_type.h"
#include "util/percentile_value.h"

namespace starrocks {

struct CelonisPercentileDiscDispatcher {
    template <LogicalType pt>
    void operator()(AggregateFuncResolver* resolver) {
        if constexpr (lt_is_datetime<pt> || lt_is_date<pt> || lt_is_arithmetic<pt> || lt_is_string<pt> ||
                      lt_is_decimal_of_any_version<pt>) {
            resolver->add_aggregate_mapping_variadic<pt, pt, PercentileState<pt>>(
                    "celonis_percentile_disc", false, AggregateFactory::MakeCelonisPercentileDiscAggregateFunction<pt>());
        }
    }
};

void AggregateFuncResolver::register_celonis() {
    add_aggregate_mapping_notnull<TYPE_BIGINT, TYPE_ARRAY>(
            "celonis_calc_bucket_width_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketWidthBoundariesAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_notnull<TYPE_DOUBLE, TYPE_ARRAY>(
            "celonis_calc_bucket_width_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketWidthBoundariesAggregateFunction<TYPE_DOUBLE>());
    add_aggregate_mapping_notnull<TYPE_DATETIME, TYPE_ARRAY>(
            "celonis_calc_bucket_width_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketWidthBoundariesAggregateFunction<TYPE_DATETIME>());

    add_aggregate_mapping_notnull<TYPE_BIGINT, TYPE_ARRAY>(
            "celonis_calc_bucket_count_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketBoundariesAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_notnull<TYPE_DOUBLE, TYPE_ARRAY>(
            "celonis_calc_bucket_count_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketBoundariesAggregateFunction<TYPE_DOUBLE>());
    add_aggregate_mapping_notnull<TYPE_DATETIME, TYPE_ARRAY>(
            "celonis_calc_bucket_count_boundaries", false,
            AggregateFactory::MakeCelonisCalcBucketBoundariesAggregateFunction<TYPE_DATETIME>());

    add_aggregate_mapping_notnull<TYPE_VARCHAR, TYPE_ARRAY>(
            "celonis_calc_string_bucket_count_boundaries", false,
            AggregateFactory::MakeCelonisCalcStringBucketCountBoundariesAggregateFunction());

    add_aggregate_mapping_notnull<TYPE_VARCHAR, TYPE_ARRAY>(
            "celonis_calc_string_bucket_width_boundaries", false,
            AggregateFactory::MakeCelonisCalcStringBucketWidthBoundariesAggregateFunction());

    add_aggregate_mapping_notnull<TYPE_STRUCT, TYPE_STRUCT>(
            "celonis_enumerate_node_paths", false, AggregateFactory::MakeCelonisEnumerateNodePathsAggregateFunction());

    add_aggregate_mapping_notnull<TYPE_STRUCT, TYPE_STRUCT>(
            "celonis_enumerate_transitive_edges", false, AggregateFactory::MakeCelonisEnumerateTransitiveEdgesAggregateFunction());

    add_aggregate_mapping_notnull<TYPE_BIGINT, TYPE_STRUCT>(
            "celonis_histogram_boundaries", false,
            AggregateFactory::MakeCelonisHistogramBoundariesAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_notnull<TYPE_DOUBLE, TYPE_STRUCT>(
            "celonis_histogram_boundaries", false,
            AggregateFactory::MakeCelonisHistogramBoundariesAggregateFunction<TYPE_DOUBLE>());
    add_aggregate_mapping_notnull<TYPE_DATETIME, TYPE_STRUCT>(
            "celonis_histogram_boundaries", false,
            AggregateFactory::MakeCelonisHistogramBoundariesAggregateFunction<TYPE_DATETIME>());
    add_aggregate_mapping_notnull<TYPE_VARCHAR, TYPE_STRUCT>(
            "celonis_histogram_boundaries", false,
            AggregateFactory::MakeCelonisHistogramBoundariesAggregateFunction<TYPE_VARCHAR>());

    add_aggregate_mapping_notnull<TYPE_BIGINT, TYPE_VARCHAR>(
            "celonis_build_abc_model", false,
            AggregateFactory::MakeCelonisBuildAbcModelAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_notnull<TYPE_DOUBLE, TYPE_VARCHAR>(
            "celonis_build_abc_model", false,
            AggregateFactory::MakeCelonisBuildAbcModelAggregateFunction<TYPE_DOUBLE>());

    add_aggregate_mapping_notnull<TYPE_ARRAY, TYPE_VARCHAR>(
            "celonis_build_multi_linear_regression_model", false,
            AggregateFactory::MakeCelonisBuildMultiLinearRegressionModelAggregateFunction<TYPE_DOUBLE>());

    add_aggregate_mapping_notnull<TYPE_VARCHAR, TYPE_STRUCT>(
            "celonis_cluster_strings", false,
            AggregateFactory::MakeCelonisClusterStringsAggregateFunction());

    add_aggregate_mapping_notnull<TYPE_ARRAY, TYPE_VARCHAR>(
            "celonis_build_kmeans_model", false,
            AggregateFactory::MakeCelonisBuildKMeansModelAggregateFunction());

    add_array_mapping_celonis<TYPE_ARRAY, TYPE_VARCHAR>("celonis_inductive_miner");

    add_general_mapping_notnull("celonis_build_linear_regression_model", false,
                                AggregateFactory::MakeCelonisBuildLinearRegressionModelAggregateFunction());

    add_general_mapping_notnull("celonis_make_factory_calendar", false,
                                AggregateFactory::MakeCelonisMakeFactoryCalendarAggregateFunction());

    add_general_mapping_notnull("celonis_make_weekday_calendar", false,
                                AggregateFactory::MakeCelonisMakeWeekdayCalendarAggregateFunction());

    add_general_mapping_notnull("celonis_make_workday_calendar", false,
                                AggregateFactory::MakeCelonisMakeWorkdayCalendarAggregateFunction());

    add_aggregate_mapping_variadic<TYPE_BIGINT, TYPE_BIGINT, CelonisModeState<TYPE_BIGINT>>(
            "celonis_mode", false, AggregateFactory::MakeCelonisModeAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_variadic<TYPE_DOUBLE, TYPE_DOUBLE, CelonisModeState<TYPE_DOUBLE>>(
            "celonis_mode", false, AggregateFactory::MakeCelonisModeAggregateFunction<TYPE_DOUBLE>());
    add_aggregate_mapping_variadic<TYPE_DATETIME, TYPE_DATETIME, CelonisModeState<TYPE_DATETIME>>(
            "celonis_mode", false, AggregateFactory::MakeCelonisModeAggregateFunction<TYPE_DATETIME>());
    add_aggregate_mapping_variadic<TYPE_VARCHAR, TYPE_VARCHAR, CelonisModeState<TYPE_VARCHAR>>(
            "celonis_mode", false, AggregateFactory::MakeCelonisModeAggregateFunction<TYPE_VARCHAR>());

    for (auto type : sortable_types()) {
        type_dispatch_all(type, CelonisPercentileDiscDispatcher(), this);
    }

    add_general_mapping_notnull("celonis_sorted_first", false,
                                AggregateFactory::MakeCelonisSortedFirstAggregateFunction());
    add_general_mapping_notnull("celonis_sorted_last", false,
                                AggregateFactory::MakeCelonisSortedLastAggregateFunction());

    add_aggregate_mapping_variadic<TYPE_BIGINT, TYPE_DOUBLE, PercentileState<TYPE_BIGINT>>(
            "celonis_trimmed_mean", false, AggregateFactory::MakeCelonisTrimmedMeanAggregateFunction<TYPE_BIGINT>());
    add_aggregate_mapping_variadic<TYPE_DOUBLE, TYPE_DOUBLE, PercentileState<TYPE_DOUBLE>>(
            "celonis_trimmed_mean", false, AggregateFactory::MakeCelonisTrimmedMeanAggregateFunction<TYPE_DOUBLE>());

    add_array_mapping_celonis<TYPE_ARRAY, TYPE_VARCHAR>("celonis_variant_stats");
    add_array_mapping_celonis<TYPE_ARRAY, TYPE_VARCHAR>("celonis_variant_stats_v2");
    add_array_mapping_celonis<TYPE_ARRAY, TYPE_VARCHAR>("celonis_graph");
    add_array_mapping_celonis<TYPE_ARRAY, TYPE_VARCHAR>("celonis_explore_process");
    add_array_mapping_celonis<TYPE_ARRAY, TYPE_STRUCT>("celonis_cluster_variants");

    auto add_product_aggregate_mapping{[this]<LogicalType LT>() {
        add_aggregate_mapping_notnull<LT, ProductResultLT<LT>>("celonis_product", false,
                                                               AggregateFactory::MakeProductAggregateFunction<LT>());
    }};

    add_product_aggregate_mapping.template operator()<TYPE_BIGINT>();
    add_product_aggregate_mapping.template operator()<TYPE_DOUBLE>();
    add_general_mapping_notnull("multi_array_agg", false, AggregateFactory::MakeMultiArrayAggAggregateFunction());
}

struct PercentileDiscDispatcher {
    template <LogicalType pt>
    void operator()(AggregateFuncResolver* resolver) {
        if constexpr (lt_is_datetime<pt> || lt_is_date<pt> || lt_is_arithmetic<pt> || lt_is_string<pt> ||
                      lt_is_decimal_of_any_version<pt>) {
            resolver->add_aggregate_mapping_variadic<pt, pt, PercentileState<pt>>(
                    "percentile_disc", false, AggregateFactory::MakePercentileDiscAggregateFunction<pt>());
        }
    }
};

struct LowCardPercentileDispatcher {
    template <LogicalType pt>
    void operator()(AggregateFuncResolver* resolver) {
        if constexpr (lt_is_datetime<pt> || lt_is_date<pt> || lt_is_arithmetic<pt> ||
                      lt_is_decimal_of_any_version<pt>) {
            resolver->add_aggregate_mapping_variadic<pt, pt, LowCardPercentileState<pt>>(
                    "percentile_disc_lc", false, AggregateFactory::MakeLowCardPercentileCntAggregateFunction<pt>());
            resolver->add_aggregate_mapping<pt, TYPE_VARCHAR, LowCardPercentileState<pt>>(
                    "percentile_build_lc", false, AggregateFactory::MakeLowCardPercentileBinAggregateFunction<pt>());
        }
    }
};

void AggregateFuncResolver::register_others() {
    add_aggregate_mapping_variadic<TYPE_BIGINT, TYPE_DOUBLE, PercentileApproxState>(
            "percentile_approx", false, AggregateFactory::MakePercentileApproxAggregateFunction());
    add_aggregate_mapping_variadic<TYPE_DOUBLE, TYPE_DOUBLE, PercentileApproxState>(
            "percentile_approx", false, AggregateFactory::MakePercentileApproxAggregateFunction());

    add_aggregate_mapping_variadic<TYPE_BIGINT, TYPE_DOUBLE, PercentileApproxState>(
            "percentile_approx_weighted", false, AggregateFactory::MakePercentileApproxWeightedAggregateFunction());
    add_aggregate_mapping_variadic<TYPE_DOUBLE, TYPE_DOUBLE, PercentileApproxState>(
            "percentile_approx_weighted", false, AggregateFactory::MakePercentileApproxWeightedAggregateFunction());

    add_aggregate_mapping<TYPE_PERCENTILE, TYPE_PERCENTILE, PercentileValue>(
            "percentile_union", false, AggregateFactory::MakePercentileUnionAggregateFunction());

    add_aggregate_mapping_variadic<TYPE_DOUBLE, TYPE_DOUBLE, PercentileState<TYPE_DOUBLE>>(
            "percentile_cont", false, AggregateFactory::MakePercentileContAggregateFunction<TYPE_DOUBLE>());
    add_aggregate_mapping_variadic<TYPE_DATETIME, TYPE_DATETIME, PercentileState<TYPE_DATETIME>>(
            "percentile_cont", false, AggregateFactory::MakePercentileContAggregateFunction<TYPE_DATETIME>());
    add_aggregate_mapping_variadic<TYPE_DATE, TYPE_DATE, PercentileState<TYPE_DATE>>(
            "percentile_cont", false, AggregateFactory::MakePercentileContAggregateFunction<TYPE_DATE>());

    for (auto type : sortable_types()) {
        type_dispatch_all(type, PercentileDiscDispatcher(), this);
    }

    for (auto type : sortable_types()) {
        type_dispatch_all(type, LowCardPercentileDispatcher(), this);
    }

    add_aggregate_mapping_variadic<TYPE_CHAR, TYPE_VARCHAR, GroupConcatAggregateState>(
            "group_concat", false, AggregateFactory::MakeGroupConcatAggregateFunction<TYPE_CHAR>());
    add_aggregate_mapping_variadic<TYPE_VARCHAR, TYPE_VARCHAR, GroupConcatAggregateState>(
            "group_concat", false, AggregateFactory::MakeGroupConcatAggregateFunction<TYPE_VARCHAR>());

    add_array_mapping<TYPE_ARRAY, TYPE_ARRAY>("retention");

    // sum, avg, distinct_sum use decimal128 as intermediate or result type to avoid overflow
    add_decimal_mapping<TYPE_DECIMAL32, TYPE_DECIMAL128>("decimal_multi_distinct_sum");
    add_decimal_mapping<TYPE_DECIMAL64, TYPE_DECIMAL128>("decimal_multi_distinct_sum");
    add_decimal_mapping<TYPE_DECIMAL128, TYPE_DECIMAL128>("decimal_multi_distinct_sum");

    // This first type is the 4th type input of windowfunnel.
    // And the 1st type is BigInt, 2nd is datetime, 3rd is mode(default 0).
    add_array_mapping<TYPE_INT, TYPE_INT>("window_funnel");
    add_array_mapping<TYPE_BIGINT, TYPE_INT>("window_funnel");
    add_array_mapping<TYPE_DATETIME, TYPE_INT>("window_funnel");
    add_array_mapping<TYPE_DATE, TYPE_INT>("window_funnel");

    add_general_mapping<AnyValueSemiState>("any_value", false, AggregateFactory::MakeAnyValueSemiAggregateFunction());
    add_general_mapping_notnull("array_agg2", false, AggregateFactory::MakeArrayAggAggregateFunctionV2());
    add_general_mapping_notnull("group_concat2", false, AggregateFactory::MakeGroupConcatAggregateFunctionV2());

    add_general_mapping_notnull("dict_merge", false, AggregateFactory::MakeDictMergeAggregateFunction());
}

} // namespace starrocks
