#pragma once

#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/celonis/agg/variant_util.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "variant.h"
#include "variant_agg.h"
#include "util/uuid_generator.h"
#include <boost/functional/hash.hpp>
#include <chrono>

namespace starrocks {

class VariantStatsState : public VariantAggregateState {
public:
    VariantStatsState() : VariantAggregateState() {}

    ~VariantStatsState() {}

    size_t update(FunctionContext* ctx, const Column** columns, size_t row_num) override {
        // As of 2023-12-20, _const_columns in merge is not aligned with _arg_types. So we pass all consts from
        // update() through serialization.
        if (ctx->is_notnull_constant_column(2)) {
            edge_count_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
        }
        if (ctx->is_notnull_constant_column(3)) {
            disable_top_variant_stats_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(3));
        }
        if (ctx->is_notnull_constant_column(4)) {
            enable_proto_encoding_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(4));
        }
        return VariantAggregateState::update(ctx, columns, row_num);
    }

    size_t serialized_size() const override {
        size_t result = sizeof(int64_t); // edge_count_
        result += sizeof(uint8_t);       // disable_top_variant_stats_
        result += sizeof(uint8_t);       // enable_proto_encoding_
        result += VariantAggregateState::serialized_size();
        return result;
    };

    void serialize(uint8_t* dst) const override {
        memcpy(dst, &edge_count_, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, &disable_top_variant_stats_, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        memcpy(dst, &enable_proto_encoding_, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        VariantAggregateState::serialize(dst);
    }

    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) override {
        auto start = std::chrono::high_resolution_clock::now();
        merging_bytes_ += len;
        ++merging_states_;
        memcpy(&edge_count_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        len -= sizeof(int64_t);
        memcpy(&disable_top_variant_stats_, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        len -= sizeof(uint8_t);
        memcpy(&enable_proto_encoding_, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        len -= sizeof(uint8_t);
        auto rv = VariantAggregateState::deserialize_and_merge(mem_pool, src, len);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        merging_microseconds_ += duration.count();
        return rv;
    }

    int64_t edge_count() const { return edge_count_; }

    bool disable_top_variant_stats() const { return disable_top_variant_stats_; }

    bool enable_proto_encoding() const { return enable_proto_encoding_; }

    uint64_t merging_microseconds() const { return merging_microseconds_; }

    uint64_t merging_bytes() const { return merging_bytes_; }

    uint64_t merging_states() const { return merging_states_; }


private:
    int64_t edge_count_ = (1LL << 32); // very large number to output all edges.
    bool disable_top_variant_stats_ = false;
    bool enable_proto_encoding_ = false;
    uint64_t merging_microseconds_ = 0;
    uint64_t merging_bytes_ = 0;
    uint64_t merging_states_ = 0;
};

class VariantStatsFinalizer : public VariantAggregateFinalizer {
public:
    using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;
    using EdgeHashSet = phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge>;

    VariantStatsFinalizer(FunctionContext* ctx, const VariantStatsState& state)
            : VariantAggregateFinalizer(ctx, static_cast<const VariantAggregateState&>(state)),
              activity_stats_(activity_map_.size()),
              edge_count_(state.edge_count()),
              disable_top_variant_stats_(state.disable_top_variant_stats()),
              enable_proto_encoding_(state.enable_proto_encoding()),
              uuid_string_(ThreadLocalUUIDGenerator::next_uuid_string()),
              merging_microseconds_(state.merging_microseconds()),
              merging_states_(state.merging_states()),
              merging_bytes_(state.merging_bytes()) {}

    std::optional<std::string> finalize(FunctionContext* ctx) override;

private:
    // Holds a reference (iterator) to a variant in the VariantHashMap.
    using VRef = VariantHashMap::const_iterator;

    // List of variant references, used to hold top-k variants per activity.
    using VList = std::vector<VRef>;

    std::optional<std::string> json_string(const std::vector<VList>& activity_top_variants, const VRef& happy) const;

    std::optional<std::string> base64_encoded_string(const std::vector<VList>& activity_top_variants, const VRef& happy,
                                                     const std::string& query_id) const;

    std::optional<std::string>
    to_string(const std::vector<VList>& activity_top_variants, const VRef& happy,
              const std::string& query_id) const;

    std::string log_prefix(const std::string& query_id) const;

    // Computes the variant that starts and ends with the most common start/end activities,
    // otherwise returns the top most frequent activity.
    int compute_happy_variant(const std::vector<VRef>& sorted) const;

    // Computes top-10 variants for each activity and happy variant.
    void compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy,
                              const std::string& query_id) const;

    std::vector<ActivityStats> activity_stats_;
    EdgeHashMap edge_map_;
    const int64_t edge_count_;
    const bool disable_top_variant_stats_;
    const bool enable_proto_encoding_;
    const std::string uuid_string_;
    const uint64_t merging_microseconds_;
    const uint64_t merging_states_;
    const uint64_t merging_bytes_;
};

/**
 * @param: [ input_column, weight_column [, edge_count [, disable_top_variant_stats [, enable_proto_encoding ] ] ] ]
 * @paramType columns: [ ARRAY_VARCHAR, BIGINT [, BIGINT [, BOOLEAN [, BOOLEAN ] ] ] ]
 * @return: json or base64 encoded binary proto string
 * variant_column: variant column.
 * weight_column: Indicates the frequency of the input (variant).
 * edge_count (optional): Limits the size of the edge table. if edge_count <= 0, edge stats is not populated and output. default = 1LL << 32 to output all the edges.
 * disable_top_variant_stats (optional): Disables top variant stats, default = false.
 * enable_proto_encoding (optional): Enable base64 encoded binary proto output, default = false.
 *
 * Used to support PQL EXPLORE_PROCESS and GRAPH
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245719/EXPLORE+PROCESS
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11248519/GRAPH+Query
 * Below are the 3 use cases
 * 1. explore_process will pass in edge_count = -1. It needs self-loop stats but it does not need overall edge count or the edge table;
 * 2. graph with edge_count = 0. It needs overall edge count but does not need the edge table;
 * 3. graph with a positive edge_count. It needs overall edge count and the edge table (trimmed by edge count).
 */
class VariantStatsAggregateFunction : public VariantAggregateFunction<VariantStatsState> {
public:
    std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                             const VariantStatsState& state) const override {
        return std::make_unique<VariantStatsFinalizer>(ctx, state);
    }

    std::string get_name() const override { return "celonis_variant_stats"; }
};

} // namespace starrocks
