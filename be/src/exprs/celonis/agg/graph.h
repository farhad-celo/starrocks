#pragma once
#include "exprs/agg/aggregate.h"
#include "variant.h"
#include "variant_stats_utils.h"

namespace starrocks {

class CelonisGraphAggregateState {
public:
    void update(FunctionContext* ctx, const Column** columns, size_t row_num);
    void serialize(uint8_t* dst) const;
    size_t serialized_size() const;
    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len);
    void finalize_to_column(FunctionContext* ctx, Column* to) const;

private:
    using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;

    int32_t maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t* memory);
    std::optional<std::string> base64_encoded_string() const;
    std::optional<std::string> json_string() const;
    std::string get_log_prefix(const std::string& query_id) const;

    SliceHashMap activity_map_;                 // activity -> index
    std::vector<ActivityStats> activity_stats_; // activity stats
    EdgeHashMap edge_stats_;                    // edge stats

    int64_t edge_count_ = std::numeric_limits<int64_t>::max();          // very large number to output all edges.
    bool enable_proto_encoding_ = false;

    // merging statistics for logging purpose
    uint64_t merging_microseconds_ = 0;
    uint64_t merging_bytes_ = 0;
    uint64_t merging_states_ = 0;
};

/**
 * @param: [ variant_column, count_column [, edge_count [, enable_proto_encoding ] ] ]
 * @paramType columns: [ ARRAY_VARCHAR, BIGINT [, BIGINT [, BOOLEAN ] ] ]
 * @return: json or base64 encoded binary proto string
 * variant_column: variant column.
 * count_column: Indicates the frequency of the input (variant).
 * edge_count (optional): Limits the size of the edge table.
 *      if edge_count == 0, returns overall edge count but does not need the edge table.
 *      if edge_count > 0, returns overall edge count and the edge table (trimmed by edge count).
 *      default = 1LL << 32 to output all the edges.
 * enable_proto_encoding (optional): Enable base64 encoded binary proto output, default = false.
 *
 * Used to support PQL GRAPH
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11248519/GRAPH+Query
 */
class CelonisGraphAggregationFunction final
    : public AggregateFunctionBatchHelper<CelonisGraphAggregateState, CelonisGraphAggregationFunction> {

public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state, size_t row_num) const
    override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                             Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                            Column* to) const override;

    std::string get_name() const override;
};

}
