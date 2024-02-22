#include "serialization_utils.h"

#include <cstring>
#include <type_traits>

#include "types/timestamp_value.h"
#include "util/slice.h"

namespace starrocks {

namespace {

// N.B: non-arithmetic types would be possible as well but this is not needed now (thus we are more restrictive)
template <typename T>
inline constexpr bool is_trivially_serializable{std::is_arithmetic_v<T>};

template <typename T>
[[nodiscard]] ByteSize do_serialization_size(const T value) {
    static_assert(is_trivially_serializable<T>);
    return sizeof(value);
}

template <>
[[nodiscard]] ByteSize do_serialization_size<TimestampValue>(const TimestampValue value) {
    // TimestampValue is just a wrapper around std::int64_t
    static_assert(std::is_same_v<decltype(TimestampValue::_timestamp), TimestampValue::type>);
    static_assert(std::is_same_v<TimestampValue::type, std::int64_t>);
    static_assert(sizeof(TimestampValue) == sizeof(std::int64_t));
    return do_serialization_size<std::int64_t>(value._timestamp);
}

template <>
[[nodiscard]] ByteSize do_serialization_size<Slice>(const Slice value) {
    const auto slice_data_size{value.size};
    return do_serialization_size<decltype(Slice::size)>(slice_data_size) + slice_data_size;
}

// serialization for trivial types
template <typename T>
[[nodiscard]] ByteSize do_serialize(MutableByteBuffer dst, const T value) {
    static_assert(is_trivially_serializable<T>);
    static constexpr auto BYTES_TO_WRITE{sizeof(T)};
    std::memcpy(dst, &value, BYTES_TO_WRITE);
    return BYTES_TO_WRITE;
}

template <>
[[nodiscard]] ByteSize do_serialize<TimestampValue>(MutableByteBuffer dst, const TimestampValue value) {
    // TimestampValue is just a wrapper around std::int64_t
    static_assert(std::is_same_v<decltype(TimestampValue::_timestamp), TimestampValue::type>);
    static_assert(std::is_same_v<TimestampValue::type, std::int64_t>);
    static_assert(sizeof(TimestampValue) == sizeof(std::int64_t));
    return do_serialize(dst, value._timestamp);
}

template <>
[[nodiscard]] ByteSize do_serialize<Slice>(MutableByteBuffer dst, const Slice value) {
    // serialization format: [data size] [data]
    const auto data_size_to_write{value.size};
    const auto bytes_written_for_data_size{do_serialize(dst, data_size_to_write)};
    dst += bytes_written_for_data_size;
    const auto* data_to_write{value.data};
    std::memcpy(dst, data_to_write, data_size_to_write);
    return bytes_written_for_data_size + data_size_to_write;
}

// deserialization for trivial types
template <typename T>
[[nodiscard]] ByteSize do_deserialize(ByteBuffer src, T& value) {
    static_assert(is_trivially_serializable<T>);
    static constexpr auto BYTES_TO_READ{sizeof(T)};
    std::memcpy(&value, src, BYTES_TO_READ);
    return BYTES_TO_READ;
}

template <>
[[nodiscard]] ByteSize do_deserialize<TimestampValue>(ByteBuffer src, TimestampValue& value) {
    // TimestampValue is just a wrapper around std::int64_t
    static_assert(std::is_same_v<Timestamp, TimestampValue::type>);
    static_assert(std::is_same_v<TimestampValue::type, std::int64_t>);
    static_assert(sizeof(TimestampValue) == sizeof(std::int64_t));
    return do_deserialize<Timestamp>(src, value._timestamp);
}

template <>
[[nodiscard]] ByteSize do_deserialize<Slice>(ByteBuffer src, Slice& value) {
    using SliceSizeType = decltype(Slice::size);
    SliceSizeType size{0};
    src += do_deserialize<SliceSizeType>(src, size);
    value = Slice{src, size};
    return sizeof(size) + size;
}

} // anonymous namespace

template <typename T>
ByteSize serialization_size(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    return do_serialization_size<T>(value);
}

template <typename T>
ByteSize serialize(MutableByteBuffer& dst, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto bytes_written{do_serialize<T>(dst, value)};
    dst += bytes_written;
    return bytes_written;
}

template <typename T>
T deserialize(ByteBuffer& src) {
    static_assert(std::is_default_constructible_v<T>);
    T deserialized_value{};
    src += do_deserialize<T>(src, deserialized_value);
    return deserialized_value;
}

template ByteSize serialization_size<Byte>(const Byte&);
template ByteSize serialization_size<std::int64_t>(const std::int64_t&);
template ByteSize serialization_size<double>(const double&);
template ByteSize serialization_size<Slice>(const Slice&);
template ByteSize serialization_size<TimestampValue>(const TimestampValue&);
template ByteSize serialization_size<std::size_t>(const std::size_t&);

template ByteSize serialize<Byte>(MutableByteBuffer& dst, const Byte& value);
template ByteSize serialize<std::int64_t>(MutableByteBuffer& dst, const std::int64_t& value);
template ByteSize serialize<double>(MutableByteBuffer& dst, const double& value);
template ByteSize serialize<Slice>(MutableByteBuffer& dst, const Slice& value);
template ByteSize serialize<TimestampValue>(MutableByteBuffer& dst, const TimestampValue& value);
template ByteSize serialize<std::size_t>(MutableByteBuffer& dst, const std::size_t& value);

template Byte deserialize<Byte>(ByteBuffer& src);
template std::int64_t deserialize<std::int64_t>(ByteBuffer& src);
template double deserialize<double>(ByteBuffer& src);
template Slice deserialize<Slice>(ByteBuffer& src);
template TimestampValue deserialize<TimestampValue>(ByteBuffer& src);
template std::size_t deserialize<std::size_t>(ByteBuffer& src);

} // namespace starrocks
