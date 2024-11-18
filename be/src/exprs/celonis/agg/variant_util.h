#pragma once

#include <optional>
#include "column/column.h"
#include "rapidjson/document.h"
#include "variant.h"

namespace starrocks {

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


} // namespace starrocks

