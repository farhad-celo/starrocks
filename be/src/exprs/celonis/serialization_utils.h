#pragma once

#include <cstdint>

namespace starrocks {

using ByteSize = std::size_t;
using Byte = std::uint8_t;
using MutableByteBuffer = Byte*;
using ByteBuffer = const Byte*;

/** Returns the number of bytes required to serialize the given value */
template <typename T>
[[nodiscard]] ByteSize serialization_size(const T& value);

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

} // namespace starrocks
