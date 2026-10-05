#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "column/binary_column.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "util/slice.h"

namespace starrocks {

// Assembles raw serialized aggregate states, e.g. to feed malformed input into merge().
class SerializedStateBuilder {
public:
    template <typename T>
    SerializedStateBuilder& put(T value) {
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof(T));
        return *this;
    }

    // Appends an activity dictionary entry: idx, size, name.
    SerializedStateBuilder& put_activity(uint32_t idx, const std::string& name) {
        put<uint32_t>(idx).put<uint32_t>(name.size());
        bytes_.insert(bytes_.end(), name.begin(), name.end());
        return *this;
    }

    std::vector<uint8_t> bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

inline std::vector<uint8_t> get_serialized_bytes(const BinaryColumn& column, size_t row = 0) {
    Slice slice = column.get_slice(row);
    return {slice.data, slice.data + slice.size};
}

// Merges `bytes` into a fresh state using a fresh FunctionContext. Returns the error reported through the context.
inline std::optional<std::string> merge_serialized_state(const AggregateFunction* func,
                                                         const std::vector<uint8_t>& bytes) {
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> ctx_holder(
            FunctionContext::create_context(nullptr, &mem_pool, FunctionContext::TypeDesc(), {}));
    FunctionContext* ctx = ctx_holder.get();
    AggDataPtr state = mem_pool.allocate_aligned(func->size(), func->alignof_size());
    func->create(ctx, state);

    auto column = BinaryColumn::create();
    column->append(Slice(bytes.data(), bytes.size()));
    func->merge(ctx, column.get(), state, 0);

    std::optional<std::string> error;
    if (ctx->has_error()) {
        error = ctx->error_msg();
    }
    func->destroy(ctx, state);
    return error;
}

inline void expect_corrupted_state(const AggregateFunction* func, const std::vector<uint8_t>& bytes,
                                   const std::string& expected_error) {
    auto error = merge_serialized_state(func, bytes);
    ASSERT_TRUE(error.has_value()) << "expected error: " << expected_error;
    EXPECT_NE(error->find("corrupted intermediate state"), std::string::npos) << *error;
    EXPECT_NE(error->find(expected_error), std::string::npos) << *error;
}

// Expects merge() to reject every proper prefix of `bytes`, except for prefixes that are complete states themselves.
inline void expect_truncated_states_rejected(const AggregateFunction* func, const std::vector<uint8_t>& bytes,
                                             const std::set<size_t>& valid_prefix_lengths = {}) {
    for (size_t len = 0; len < bytes.size(); ++len) {
        if (valid_prefix_lengths.count(len) > 0) {
            continue;
        }
        std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + len);
        EXPECT_TRUE(merge_serialized_state(func, prefix).has_value())
                << "prefix of " << len << " out of " << bytes.size() << " bytes was accepted";
    }
}

} // namespace starrocks
