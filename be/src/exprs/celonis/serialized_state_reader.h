#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "common/compiler_util.h"
#include "util/slice.h"

namespace starrocks {

/**
 * Bounds-checked cursor over a serialized aggregate state. The state arrives from other backends or from spilled data
 * and must be treated as untrusted: every read fails instead of running past the end of the buffer.
 */
class SerializedStateReader {
public:
    SerializedStateReader(const uint8_t* data, size_t len) : pos_(data), end_(data + len) {}

    size_t remaining() const { return end_ - pos_; }
    bool at_end() const { return pos_ == end_; }

    template <typename T>
    [[nodiscard]] bool read(T* value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (UNLIKELY(remaining() < sizeof(T))) {
            return false;
        }
        memcpy(value, pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    // Reads a length prefix of type SizeT, then points `out` at that many bytes. No copy; `out` aliases the input.
    template <typename SizeT>
    [[nodiscard]] bool read_length_prefixed(Slice* out) {
        static_assert(std::is_unsigned_v<SizeT>);
        if (UNLIKELY(remaining() < sizeof(SizeT))) {
            return false;
        }
        SizeT len;
        memcpy(&len, pos_, sizeof(SizeT));
        if (UNLIKELY(remaining() - sizeof(SizeT) < len)) {
            return false;
        }
        pos_ += sizeof(SizeT);
        *out = Slice(pos_, len);
        pos_ += len;
        return true;
    }

private:
    const uint8_t* pos_;
    const uint8_t* end_;
};

} // namespace starrocks
