#pragma once
#include "exprs/agg/aggregate.h"
#include "variant.h"
#include "variant_stats_utils.h"

namespace starrocks {

class CelonisExploreProcessAggregateState {
public:
    void update(FunctionContext* ctx, const Column** columns, size_t row_num);
    void serialize_to_column(FunctionContext* ctx, Column* to) const;
    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len);
    void finalize_to_column(FunctionContext* ctx, Column* to) const;

private:
    // Drops variants whose counts are smaller than min_variant_count_threshold_on_leaf_. When intending to drop a
    // variant, it is checked whether dropping it will incur a "phantom" activity in the serialized data. If yes, the
    // variant won't be dropped. A phantom activity means that it's not associated with any variant.
    // Variants are sorted ascendingly before the drop happens.
    VariantHashMap get_trimmed_variant_map(const std::string& log_prefix) const;
    void serialize(uint8_t* dst, const VariantHashMap& variant_map) const;
    size_t serialized_size(const VariantHashMap& variant_map) const;

    std::optional<std::string> base64_encoded_string(const VariantAnalysisResult& variant_analysis,
                                                     const std::string& log_prefix) const;
    std::optional<std::string> json_string(const VariantAnalysisResult& variant_analysis) const;
    static std::string get_log_prefix(FunctionContext* ctx);

    SliceHashMap activity_map_;                         // activity -> index
    VariantHashMap variant_map_;                        // variant -> count
    std::vector<ActivityStats> activity_stats_;         // activity stats
    std::vector<size_t> activity_self_loop_count_case_; // activity self loop count case

    int64_t min_variant_count_threshold_on_leaf_ = 0;
    bool enable_proto_encoding_ = false;

    // merging statistics for logging purpose
    uint64_t merging_microseconds_ = 0;
    uint64_t merging_bytes_ = 0;
    uint64_t merging_states_ = 0;

    // Reusable tracking containers to avoid repeated allocations in update()
    // Using vector<uint8_t> instead of vector<bool> for performance reasons. Logically it's a vector of bools.
    // Tracks which activities have been updated in current case
    std::vector<uint8_t> activity_updated_count_cases_;
    // Tracks which activities' self-loops have been updated in current case
    std::vector<uint8_t> activity_self_loop_updated_count_cases_;
};

/**
 * @param: [ variant_column, count_column, min_variant_count_threshold_on_leaf, enable_proto_encoding ]
 * @paramType columns: [ ARRAY_VARCHAR, BIGINT, BIGINT, BOOLEAN ]
 * @return: json or base64 encoded binary proto string
 * variant_column: variant column.
 * count_column: Indicates the frequency of the input (variant).
 * min_variant_count_threshold_on_leaf: Used to drop low frequency variants on leaf nodes. On a leaf
 *   aggregation node, if a variant's frequency is smaller than min_variant_count_threshold_on_leaf, it gets dropped
 *   locally. Set this value when the number of distinct variants is too huge to be handled by the root aggregation
 *   node. Set to <= 1 (e.g., 0 or 1) to not drop any variants. Note that:
 *     - Activity stats are computed on leafs, so variant dropping doesn't impact their results.
 *     - Variants are sorted by count ascendingly before getting dropped.
 *     - When intending to drop a variant, it is checked whether dropping it will incur a "phantom" activity in the
 *       serialized data. If yes, the variant won't be dropped. A phantom activity means that it's not associated with
 *       any variant.
 *
 * enable_proto_encoding: Enable base64 encoded binary proto output.
 *
 * Used to support PQL EXPLORE_PROCESS
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245719/EXPLORE_PROCESS
 */
class CelonisExploreProcessAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisExploreProcessAggregateState,
                                              CelonisExploreProcessAggregationFunction> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                             Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override;

    std::string get_name() const override;
};

} // namespace starrocks
