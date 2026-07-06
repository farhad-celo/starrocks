#pragma once

#include "column/hash_set.h"
#include "rapidjson/document.h"
#include "util/phmap/phmap.h"

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
using VariantHashMap = phmap::flat_hash_map<Variant, int32_t, HashOnVariant, EqualOnVariant>;

} // namespace starrocks
