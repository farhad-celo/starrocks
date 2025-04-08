#include "serialization_utils.h"

#include <cstring>
#include <type_traits>

#include "types/timestamp_value.h"
#include "util/slice.h"

namespace starrocks {

namespace {

template <typename T>
[[nodiscard]] ByteSize do_serialization_size(const T /*value*/) {
    return details::do_static_serialization_size<T>();
}

template <>
[[nodiscard]] ByteSize do_serialization_size<Slice>(const Slice value) {
    const auto slice_data_size{value.size};
    return do_serialization_size<decltype(Slice::size)>(slice_data_size) + slice_data_size;
}

// serialization for trivial types
template <typename T>
[[nodiscard]] ByteSize do_serialize(MutableByteBuffer dst, const T value) {
    static_assert(TriviallySerializable<T>);
    static constexpr auto BYTES_TO_WRITE{sizeof(T)};
    std::memcpy(dst, &value, BYTES_TO_WRITE);
    return BYTES_TO_WRITE;
}

template <>
[[nodiscard]] ByteSize do_serialize<TimestampValue>(MutableByteBuffer dst, const TimestampValue value) {
    static_assert(TriviallySerializable<TimestampValue>);
    return do_serialize(dst, value._timestamp);
}

template <>
[[nodiscard]] ByteSize do_serialize<DateValue>(MutableByteBuffer dst, const DateValue value) {
    static_assert(TriviallySerializable<DateValue>);
    return do_serialize(dst, value._julian);
}
template <>
[[nodiscard]] ByteSize do_serialize<DecimalV2Value>(MutableByteBuffer dst, const DecimalV2Value value) {
    static_assert(TriviallySerializable<DecimalV2Value>);
    return do_serialize(dst, value.value());
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
    static_assert(TriviallySerializable<T>);
    static constexpr auto BYTES_TO_READ{sizeof(T)};
    std::memcpy(&value, src, BYTES_TO_READ);
    return BYTES_TO_READ;
}

template <>
[[nodiscard]] ByteSize do_deserialize<TimestampValue>(ByteBuffer src, TimestampValue& value) {
    static_assert(TriviallySerializable<TimestampValue>);
    return do_deserialize<details::serialized_type_t<TimestampValue>>(src, value._timestamp);
}

template <>
[[nodiscard]] ByteSize do_deserialize<DateValue>(ByteBuffer src, DateValue& value) {
    static_assert(TriviallySerializable<DateValue>);
    return do_deserialize<details::serialized_type_t<DateValue>>(src, value._julian);
}

template <>
[[nodiscard]] ByteSize do_deserialize<DecimalV2Value>(ByteBuffer src, DecimalV2Value& value) {
    static_assert(TriviallySerializable<DecimalV2Value>);
    return do_deserialize<details::serialized_type_t<DecimalV2Value>>(src, value.value());
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

template ByteSize serialization_size<bool>(const bool&);
template ByteSize serialization_size<int8_t>(const int8_t&);
template ByteSize serialization_size<int16_t>(const int16_t&);
template ByteSize serialization_size<int32_t>(const int32_t&);
template ByteSize serialization_size<int64_t>(const int64_t&);
template ByteSize serialization_size<int128_t>(const int128_t&);
template ByteSize serialization_size<Byte>(const Byte&);
template ByteSize serialization_size<std::size_t>(const std::size_t&);
template ByteSize serialization_size<float>(const float&);
template ByteSize serialization_size<double>(const double&);
template ByteSize serialization_size<Slice>(const Slice&);
template ByteSize serialization_size<TimestampValue>(const TimestampValue&);
template ByteSize serialization_size<DateValue>(const DateValue&);
template ByteSize serialization_size<DecimalV2Value>(const DecimalV2Value&);

template ByteSize serialize<bool>(MutableByteBuffer& dst, const bool& value);
template ByteSize serialize<int8_t>(MutableByteBuffer& dst, const int8_t& value);
template ByteSize serialize<int16_t>(MutableByteBuffer& dst, const int16_t& value);
template ByteSize serialize<int32_t>(MutableByteBuffer& dst, const int32_t& value);
template ByteSize serialize<int64_t>(MutableByteBuffer& dst, const int64_t& value);
template ByteSize serialize<int128_t>(MutableByteBuffer& dst, const int128_t& value);
template ByteSize serialize<Byte>(MutableByteBuffer& dst, const Byte& value);
template ByteSize serialize<std::size_t>(MutableByteBuffer& dst, const std::size_t& value);
template ByteSize serialize<float>(MutableByteBuffer& dst, const float& value);
template ByteSize serialize<double>(MutableByteBuffer& dst, const double& value);
template ByteSize serialize<Slice>(MutableByteBuffer& dst, const Slice& value);
template ByteSize serialize<TimestampValue>(MutableByteBuffer& dst, const TimestampValue& value);
template ByteSize serialize<DateValue>(MutableByteBuffer& dst, const DateValue& value);
template ByteSize serialize<DecimalV2Value>(MutableByteBuffer& dst, const DecimalV2Value& value);

template bool deserialize<bool>(ByteBuffer& src);
template int8_t deserialize<int8_t>(ByteBuffer& src);
template int16_t deserialize<int16_t>(ByteBuffer& src);
template int32_t deserialize<int32_t>(ByteBuffer& src);
template int64_t deserialize<int64_t>(ByteBuffer& src);
template int128_t deserialize<int128_t>(ByteBuffer& src);
template Byte deserialize<Byte>(ByteBuffer& src);
template std::size_t deserialize<std::size_t>(ByteBuffer& src);
template float deserialize<float>(ByteBuffer& src);
template double deserialize<double>(ByteBuffer& src);
template Slice deserialize<Slice>(ByteBuffer& src);
template TimestampValue deserialize<TimestampValue>(ByteBuffer& src);
template DateValue deserialize<DateValue>(ByteBuffer& src);
template DecimalV2Value deserialize<DecimalV2Value>(ByteBuffer& src);

} // namespace starrocks