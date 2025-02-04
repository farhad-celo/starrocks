#pragma once

#include "column/hash_set.h"
#include "rapidjson/document.h"
#include "util/phmap/phmap.h"
#include "util/slice.h"
#include <boost/functional/hash.hpp>

namespace starrocks {

// A sequence of integer activity ids.
struct Variant {
    size_t hash{0};
    std::vector<int32_t> data;

    Variant() = default;

    explicit Variant(int n) { data.reserve(n); }

    // Appends an activity to the variant.
    void add(int32_t index, size_t element_hash);

    // Returns true if the variants are equal with remapping of activities through map.
    // Only used in unit tests.
    bool equal_remap_for_testing(const Variant& other, const std::vector<int32_t>& map) const;

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

struct EqualOnVariant {
    bool operator()(const Variant& x, const Variant& y) const { return x.hash == y.hash && x.data == y.data; }
};

struct HashOnVariant {
    std::size_t operator()(const Variant& x) const { return x.hash; }
};

using SliceHashMap = phmap::flat_hash_map<SliceWithHash, int32_t, HashOnSliceWithHash, EqualOnSliceWithHash>;
using VariantHashMap = phmap::flat_hash_map<Variant, size_t, HashOnVariant, EqualOnVariant>;

struct VariantCount {
    VariantCount(std::vector<int32_t> v, size_t c) : variant(std::move(v)), count(c) {}

    std::vector<int32_t> variant;
    size_t count;

    bool operator<(const VariantCount& other) const { return variant < other.variant; }
};

using Variants = std::vector<VariantCount>;

// A pair of activities that appear together in a variant.
struct Edge {
    size_t hash;
    int32_t src;
    int32_t dst;

    Edge() : hash(0), src(-1), dst(-1) {}

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

} // namespace starrocks
