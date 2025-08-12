#pragma once

#include "util/hash_util.hpp"

namespace starrocks {

/*
 * Saola returns an 'ID' column which is marked as deprecated but is nonetheless critical for certain functionality. For
 * our implementation we just take an int32 hashcode with some fixup at the end to ensure that we return a positive
 * value.
 */
// TODO(j.kim): Make //celostar/pql2sql/ID.java and this consistent.
class Id {
public:
    static int32_t get(const Slice& slice) {
        int32_t hash =
                HashUtil::murmur_hash3_32(slice.data, static_cast<int32_t>(slice.size), HashUtil::MURMUR3_32_SEED);
        hash &= 0x7FFFFFFF;
        return hash == 0 ? 1 : hash;
    }
};

} // namespace starrocks