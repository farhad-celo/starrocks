#pragma once

#include "column/hash_set.h"
#include "exprs/celonis/variant.h"
#include "exprs/celonis/variant_agg.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"

namespace starrocks {

// A pair of activities that appear together in a variant.
struct Edge {
    size_t hash;
    int32_t src;
    int32_t dst;

    Edge(int32_t in_src, int32_t in_dst) : src(in_src), dst(in_dst) {
        hash = std::hash<int32_t>()(src);
        HashUtil::hash_combine(hash, std::hash<int32_t>()(dst));
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
    using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;
    using EdgeHashSet = phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge>;

    VariantStatsFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : VariantAggregateFinalizer(ctx, state), activity_stats_(activity_map_.size()) {}

    std::string finalize() override;

private:
    // Holds a reference (iterator) to a variant in the VariantHashMap.
    using VRef = VariantHashMap::const_iterator;

    // List of variant references, used to hold top-k variants per activity.
    using VList = std::vector<VRef>;

    std::string json_string(std::vector<VList>& activity_top_variants, VRef& happy) const;

    // Computes the variant that starts and ends with the most common start/end activities,
    // otherwise returns the top most frequent activity.
    int compute_happy_variant(const std::vector<VRef>& sorted) const;

    // Computes top-10 variants for each activity.
    int compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::vector<ActivityStats> activity_stats_;
    EdgeHashMap edge_map_;
};

// Extends VariantAggregateFunction and calculates statistics of activities and edges.
// TODO(hagonzal): Return json column. Now it returns a string column with json.
// TODO(hagonzal): add option to compute approximate top-k variants, now it returns exact top-k.
class VariantStatsAggregateFunction : public VariantAggregateFunction {
public:
    std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                             const VariantAggregateState& state) const override {
        return std::make_unique<VariantStatsFinalizer>(ctx, state);
    }

    std::string get_name() const override { return "celonis_variant_stats"; }
};

} // namespace starrocks
