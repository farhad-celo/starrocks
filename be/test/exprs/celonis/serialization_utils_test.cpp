#include "exprs/celonis/serialization_utils.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "column/column_hash.h"
#include "types/timestamp_value.h"
#include "util/slice.h"

namespace starrocks {

static constexpr std::size_t N{12};
static constexpr std::array<char, N> BFR{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};

TEST(SerializationUtilsTest, serialization_size_tests) {
    ASSERT_EQ(8, serialization_size(std::size_t{}));
    ASSERT_EQ(8, serialization_size(std::int64_t{}));
    ASSERT_EQ(8, serialization_size(double{}));
    ASSERT_EQ(8 + N, serialization_size(Slice{BFR.data(), N}));
    ASSERT_EQ(8, serialization_size(TimestampValue{}));
}

template <typename T>
void execute_serialization_and_deserialization_test(const T& value);

TEST(SerializationUtilsTest, serialization_and_deserialization_test) {
    execute_serialization_and_deserialization_test(std::size_t{42});
    execute_serialization_and_deserialization_test(std::int64_t{-42});
    execute_serialization_and_deserialization_test(double{3.14});
    execute_serialization_and_deserialization_test(Slice{BFR.data(), N});
    execute_serialization_and_deserialization_test(TimestampValue::create(2023, 11, 16, 0, 0, 0, 0));
}

template <typename T>
void execute_serialization_and_deserialization_test(const T& value) {
    const auto required_bfr_size{serialization_size(value)};
    auto bfr{std::make_unique<std::uint8_t[]>(required_bfr_size)};
    {
        auto* bfr_ptr{bfr.get()};
        const auto bytes_written{serialize(bfr_ptr, value)};
        ASSERT_EQ(required_bfr_size, bytes_written);
        ASSERT_EQ(bfr.get() + required_bfr_size, bfr_ptr);
    }
    {
        const auto* bfr_ptr{bfr.get()};
        const auto deserialized_value{deserialize<T>(bfr_ptr)};
        ASSERT_EQ(bfr.get() + required_bfr_size, bfr_ptr);
        // After deserialization, we should get the exact same value as initially serialized.
        if constexpr (std::is_same_v<Slice, T>) {
            ASSERT_TRUE(SliceEqual{}(value, deserialized_value));
        } else {
            ASSERT_EQ(value, deserialized_value);
        }
    }
}

} // namespace starrocks
