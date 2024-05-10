#pragma once

#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "variant.h"
#include "variant_agg.h"
#include <boost/functional/hash.hpp>

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
            disable_top_variant_stats_= ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(3));
        }
        if (ctx->is_notnull_constant_column(4)) {
            enable_proto_encoding_= ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(4));
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
        uint8_t dtvs = disable_top_variant_stats_;
        memcpy(dst, &dtvs, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        uint8_t epe = enable_proto_encoding_;
        memcpy(dst, &epe, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        VariantAggregateState::serialize(dst);
    }

    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) override {
        memcpy(&edge_count_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        len -= sizeof(int64_t);
        uint8_t dtvs;
        memcpy(&dtvs, src, sizeof(uint8_t));
        disable_top_variant_stats_ = dtvs;
        src += sizeof(uint8_t);
        len -= sizeof(uint8_t);
        uint8_t epe;
        memcpy(&epe, src, sizeof(uint8_t));
        enable_proto_encoding_ = epe;
        src += sizeof(uint8_t);
        len -= sizeof(uint8_t);
        return VariantAggregateState::deserialize_and_merge(mem_pool, src, len);
    }

    int64_t edge_count() const { return edge_count_; }
    bool disable_top_variant_stats() const { return disable_top_variant_stats_; }
    bool enable_proto_encoding() const { return enable_proto_encoding_; }

private:
    int64_t edge_count_ = -1;
    bool disable_top_variant_stats_ = false;
    bool enable_proto_encoding_ = false;
};

// A pair of activities that appear together in a variant.
struct Edge {
    size_t hash;
    int32_t src;
    int32_t dst;

    Edge(int32_t in_src, int32_t in_dst) : src(in_src), dst(in_dst) {
        boost::hash<std::tuple<int32_t, int32_t>> hasher;
        hash = hasher({src, dst});
    }

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

struct EqualOnEdge {
    bool operator()(const Edge& x, const Edge& y) const { return x.src == y.src && x.dst == y.dst; }
};

struct HashOnEdge {
    std::size_t operator()(const Edge& x) const { return x.hash; }
};

// Basic statistics on an Edge.
struct EdgeStats {
    size_t count{0};      // Number of times this edge appears
    size_t count_case{0}; // Number distinct cases this edge appears in

    bool equal(const EdgeStats& other) { return count == other.count && count_case == other.count_case; }

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

// Basic statistics on an activity.
struct ActivityStats {
    size_t count{0};       // Number of times the activity appears (can be > 1 per case)
    size_t count_case{0};  // Number of distinct cases that contain the activity.
    size_t count_start{0}; // Number of times the activity appears at the start of a case
    size_t count_end{0};   // Number of times the activity appears at the end of a case

    bool equal(const ActivityStats& other) {
        return count == other.count && count_case == other.count_case && count_start == other.count_start &&
               count_end == other.count_end;
    }

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

class VariantStatsFinalizer : public VariantAggregateFinalizer {
public:
    using EdgeHashMap = std::unordered_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;
    using EdgeHashSet = phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge>;

    VariantStatsFinalizer(FunctionContext* ctx, const VariantStatsState& state)
            : VariantAggregateFinalizer(ctx, static_cast<const VariantAggregateState&>(state)),
              activity_stats_(activity_map_.size()),
              edge_count_(state.edge_count()),
              disable_top_variant_stats_(state.disable_top_variant_stats()),
              enable_proto_encoding_(state.enable_proto_encoding()) {}

    std::string finalize() override;

private:
    // Holds a reference (iterator) to a variant in the VariantHashMap.
    using VRef = VariantHashMap::const_iterator;

    // List of variant references, used to hold top-k variants per activity.
    using VList = std::vector<VRef>;

    std::string json_string(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::string base64_encoded_string(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::string to_string(std::vector<VList>& activity_top_variants, VRef& happy) const;

    // Computes the variant that starts and ends with the most common start/end activities,
    // otherwise returns the top most frequent activity.
    int compute_happy_variant(const std::vector<VRef>& sorted) const;

    // Computes top-10 variants for each activity and happy variant.
    void compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::vector<ActivityStats> activity_stats_;
    EdgeHashMap edge_map_;
    const int64_t edge_count_;
    const bool disable_top_variant_stats_;
    const bool enable_proto_encoding_;
};

// Extends VariantAggregateFunction and calculates statistics of activities and edges.
// TODO(hagonzal): Return json column. Now it returns a string column with json.
// TODO(hagonzal): add option to compute approximate top-k variants, now it returns exact top-k.
/**
 * @param: [ input_column, weight_column [, edge_count ] ]
 * @paramType columns: [ BIGINT, BIGINT [, BIGINT [, BOOLEAN [, BOOLEAN ] ] ] ]
 * @return: json string
 * weight_column : Indicates the frequency of the input(variant)
 * edge_count (optional) : Limits the size of the edge table. If < 0, there is no limit.
 * disable_top_variant_stats (optional) : Disables top variant stats
 * enable_proto_encoding (optional): Enable base64 encoded binary proto output
 *
 * Used to support PQL EXPLORE_PROCESS and GRAPH
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245719/EXPLORE+PROCESS
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11248519/GRAPH+Query
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
