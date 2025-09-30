#pragma once

#include "column/column.h"
#include "rapidjson/document.h"
#include "variant.h"

namespace starrocks {

constexpr size_t MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES = std::numeric_limits<int16_t>::max();

// Basic statistics on an Edge.
struct EdgeStats {
    size_t count{0};      // Number of times this edge appears
    size_t count_case{0}; // Number distinct cases this edge appears in
    const Variant* last_variant = nullptr; // last variant to update count_case
    const std::vector<int32_t>* last_variant_v2 = nullptr; // last variant to update count_case, used in variant_stats_v2

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

using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;

// Holds a reference (iterator) to a variant in the VariantHashMap.
using VRef = VariantHashMap::const_iterator;
// List of variant references, used to hold top-k variants per activity.
using VList = std::vector<VRef>;

struct VariantAnalysisResult {
    VRef happy;
    std::vector<VList> activity_top_variants;
};

// TODO(xingyuan): Move analyze_variants in variant_stats_v2 to this utils file as well
/**
 * Analyzes variants to compute happy path and top-10 variants per activity. Used to support EXPLORE_PROCESS PQL function.
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245719/EXPLORE_PROCESS
 *
 * @param variant_counts A map from each variant to its frequency.
 * @param activity_map   A lookup table that maps an activity's string name to its unique integer index.
 * @param activity_stats A vector containing statistics for each activity, indexed by the activity's ID from `activity_map`.
 * @param log_prefix     A string prepended to any log messages.
 *
 * @return An `VariantAnalysisResult` object containing a happy path and top-10 variants per activity. Variants are
 *         represented as const references (iterators) to elements in the original VariantHashMap.
 */
VariantAnalysisResult analyze_variants_for_explore_process(const VariantHashMap& variant_counts,
    const SliceHashMap& activity_map, const std::vector<ActivityStats>& activity_stats,
    const std::string& log_prefix);

} // namespace starrocks

