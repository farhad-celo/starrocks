#pragma once

#include "column/column.h"
#include "rapidjson/document.h"
#include "runtime/mem_pool.h"
#include "variant.h"

// Forward-declare protobuf message classes
namespace celonis {
namespace accelerator {
class Statistics;
}
} // namespace celonis

namespace starrocks {

constexpr size_t MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES = std::numeric_limits<int16_t>::max();

// Basic statistics on an Edge.
struct EdgeStats {
    size_t count{0};                       // Number of times this edge appears
    size_t count_case{0};                  // Number distinct cases this edge appears in
    const Variant* last_variant = nullptr; // last variant to update count_case
    const std::vector<int32_t>* last_variant_v2 =
            nullptr; // last variant to update count_case, used in variant_stats_v2

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

// TODO(xingyuan): refactor cluster_variants.{h,cpp} to use maybe_add_activity

/**
 * @brief Finds an activity in a map, adding it if not present, and returns its index and hash.
 *
 * This function implements a "find or insert" pattern for activities. It checks if the activity represented by the
 * input Slice `activity` already exists in the `activity_map`.
 *
 * - If the activity is new, it allocates memory for the slice's data from the provided `mem_pool`, copies the data, and
 * inserts the new activity into the `activity_map`. A new unique index is assigned (based on the current map size),
 * and the `memory` usage counter is updated.
 *
 * - If the activity already exists, it retrieves its existing index from the map.
 *
 * Note that activity indices are assigned sequentially starting from 0 based on insertion order.
 *
 * @param[in,out] activity_map The hash map that stores unique activities and maps them to integer indices. This map is
 *                             modified if a new activity is added.
 * @param activity A non-owning view of the activity data to look up or add.
 * @param mem_pool A pointer to a memory pool used for allocating storage for new activity data.
 * @param[out] memory A pointer to a memory usage counter, which is incremented when a new activity is added to the map.
 * @return A std::pair containing the activity's unique integer index and its hash value.
 */
std::pair<int32_t, size_t> maybe_add_activity(SliceHashMap& activity_map, const Slice& activity, MemPool* mem_pool,
                                              size_t* memory);

/**
 * @brief Serializes an activity map into a byte buffer.
 *
 * Serialization format:
 * - `uint32_t num_activities`: The number of unique activities.
 * - For each activity (loop `num_activities` times):
 * - `uint32_t idx`: The unique identifier (index) for the activity.
 * - `uint32_t size`: The length of the activity name string in bytes.
 * - `char[] activity_name`: The raw bytes of the activity name string.
 *
 * @param dst A pointer to the destination buffer where the serialized data will be written.
 * @param activity_map The constant reference to the activity map to be serialized.
 * @return A pointer to the byte immediately following the last written byte.
 */
uint8_t* serialize_activity_map(uint8_t* dst, const SliceHashMap& activity_map);
size_t get_serialized_size(const SliceHashMap& activity_map);
/**
 * @brief Deserializes an activity map from a byte buffer and merges it with a local map.
 *
 * This function reads a serialized representation of an activity map from the `src` buffer. For each deserialized
 * activity, it calls `maybe_add_activity` to find the activity in the local `activity_map` or insert it if it's new.
 *
 * The function populates `index_vector` as a translation map, which maps an activity's index from the source data to
 * its corresponding index and hash in the local map.
 *
 * @param src A pointer to the buffer containing the serialized activity map.
 * @param[out] index_vector An output vector that will be populated with a mapping from source indices to local {index, hash} pairs.
 * @param[in,out] activity_map The local activity map into which deserialized activities are merged.
 * @param mem_pool A pointer to a memory pool used for allocating storage for new activity data.
 * @param[out] memory A pointer to a memory usage counter, which is incremented when a new activity is added to the map.
 * @return A pointer to the position in the source buffer immediately after the consumed data.
 */
const uint8_t* deserialize_activity_map_and_merge(const uint8_t* src,
                                                  std::vector<std::pair<int32_t, size_t>>& index_vector,
                                                  SliceHashMap& activity_map, MemPool* mem_pool, size_t* memory);

/**
 * Serializes a variant map into a byte buffer. It's a map from variants to their counts.
 *
 * Serialization format:
 * - `size_t num_variants`: The number of unique variants.
 * - For each variant (loop `num_variants` times):
 * - `size_t count`: The total number of times this variant occurs.
 * - `uint32_t num_activities`: The number of activities in this variant.
 * - For each activity (loop `num_activities` times):
 * - `uint32_t activity_idx`: Activity index.
 *
 * CAVEAT: Deserialization of the variant map MUST HAPPEN AFTER the deserialization of activity map, because it requires
 * a local_idx -> glocal_idx map that's built by the deserialization of activity map.
 *
 * @param dst A pointer to the destination buffer where the serialized data will be written.
 * @param variant_map The constant reference to the variant map to be serialized.
 * @return A pointer to the byte immediately following the last written byte.
 */
uint8_t* serialize_variant_map(uint8_t* dst, const VariantHashMap& variant_map);
size_t get_serialized_size(const VariantHashMap& variant_map);
/**
 * @brief Deserializes a variant map from a byte buffer and merges it into the local map.
 *
 * @param src A pointer to the buffer containing the serialized variant map data.
 * @param index_vector A vector that maps activity indices from the source data to local {index, hash} pairs. This must
 *                     be populated before calling this function.
 * @param[out] variant_map The local variant hash map into which the deserialized variants will be merged.
 * @return A pointer to the position in the source buffer immediately after the last byte read.
 */
const uint8_t* deserialize_variant_map_and_merge(const uint8_t* src,
                                                 const std::vector<std::pair<int32_t, size_t>>& index_vector,
                                                 VariantHashMap& variant_map);

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
                                                           const SliceHashMap& activity_map,
                                                           const std::vector<ActivityStats>& activity_stats,
                                                           const std::string& log_prefix);

void build_variant_analysis_proto(const VariantAnalysisResult& variant_analysis_result,
                                  ::celonis::accelerator::Statistics& statistics_proto, const std::string& log_prefix);

void build_variant_analysis_json(const VariantAnalysisResult& variant_analysis_result, rapidjson::Document& d,
                                 rapidjson::Document::AllocatorType& allocator);

} // namespace starrocks
