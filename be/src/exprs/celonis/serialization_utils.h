#pragma once

#include <cstdint>
#include <type_traits>

#include "column/type_traits.h"

namespace starrocks {

using ByteSize = std::size_t;
using Byte = std::uint8_t;
using MutableByteBuffer = Byte*;
using ByteBuffer = const Byte*;

template <typename T>
concept SerializableType = requires(T t) {
    { serialization_size<T>(t) } -> std::same_as<ByteSize>;
};

/** Returns the number of bytes required to serialize the given value */
template <typename T>
[[nodiscard]] ByteSize serialization_size(const T& value);

/** Returns the number of bytes required to serialize the given value. This is a constexpr version for types where
* the serialized size is not data-dependent. */
template <typename T>
[[nodiscard]] constexpr ByteSize static_serialization_size();

/**
 * @brief Serializes the given value (as binary format) into the given target buffer
 * @param dst the target buffer pointer. Pointer is incremented by the number of written bytes.
 * @tparam T the type of the given value to serialize.
 * Supported types: int64_t (BIGINT), double (DOUBLE), Slice (VARCHAR), TimestampValue (DATETIME), std::size_t
 * @param value the given value to serialize
 * @return total number of bytes written
 */
template <typename T>
ByteSize serialize(MutableByteBuffer& dst, const T& value);

/** Deserializes a value from the given source buffer and returns it with the given type */
template <typename T>
[[nodiscard]] T deserialize(ByteBuffer& src);

/**
 * Implementation section
 * The implementations of static_serialization_size must live in the header file so that it can be constexpr.
 */

namespace details {

template <typename T>
struct serialized_type {
    static_assert(std::is_arithmetic_v<T>);
    using type = T;
};

template <>
struct serialized_type<TimestampValue> {
    // TimestampValue is just a wrapper around std::int64_t
    static_assert(std::is_same_v<decltype(TimestampValue::_timestamp), TimestampValue::type>);
    static_assert(std::is_same_v<TimestampValue::type, std::int64_t>);
    static_assert(sizeof(TimestampValue) == sizeof(std::int64_t));
    using type = std::int64_t;
};

template <>
struct serialized_type<DateValue> {
    // DateValue is just a wrapper around std::int32_t
    static_assert(std::is_same_v<decltype(DateValue::_julian), DateValue::type>);
    static_assert(std::is_same_v<DateValue::type, std::int32_t>);
    static_assert(sizeof(DateValue) == sizeof(std::int32_t));
    using type = std::int32_t;
};

template <>
struct serialized_type<DecimalV2Value> {
    // DecimalV2Value is just a wrapper around int128_t
    static_assert(sizeof(DecimalV2Value) == sizeof(int128_t));
    using type = int128_t;
};

template <typename T>
using serialized_type_t = serialized_type<T>::type;

template <typename T>
[[nodiscard]] constexpr ByteSize do_static_serialization_size() {
    return sizeof(serialized_type_t<T>);
}

} // namespace details

template <typename T>
concept TriviallySerializable = std::is_arithmetic_v<details::serialized_type_t<T>>;

template <typename T>
constexpr ByteSize static_serialization_size() {
    static_assert(std::is_trivially_copyable_v<T>);
    return details::do_static_serialization_size<T>();
}

} // namespace starrocks