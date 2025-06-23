#include "exprs/celonis/stringhash.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "exprs/celonis/match_pattern_util.h"

// The implementation is copied from Saola: cpm-query-engine/blob/01a65c8ebe07c10cd7fda76eb3be95c070aada79/query-engine/src/main/native/cpm-accelerator/modules/common/stringhasher.cpp
extern "C" {
#include "blake2.h"
}

namespace starrocks {

namespace {

constexpr const std::array<char, 65> BASE64_TABLE{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

void base64_encode(const unsigned char* const src, const size_t len, char* const out) {
    char* pos{};
    const unsigned char* end{};
    const unsigned char* in{};

    end = src + len;
    in = src;
    pos = out;
    while (end - in >= 3) {
        *pos++ = BASE64_TABLE[in[0] >> 2];
        *pos++ = BASE64_TABLE[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *pos++ = BASE64_TABLE[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *pos++ = BASE64_TABLE[in[2] & 0x3f];
        in += 3;
    }

    if ((end - in) != 0) {
        *pos++ = BASE64_TABLE[in[0] >> 2];
        if (end - in == 1) {
            *pos++ = BASE64_TABLE[(in[0] & 0x03) << 4];  // NOLINT(bugprone-misplaced-widening-cast)
            *pos++ = '=';
        } else {
            *pos++ = BASE64_TABLE[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *pos++ = BASE64_TABLE[(in[1] & 0x0f) << 2];  // NOLINT(bugprone-misplaced-widening-cast)
        }
        *pos++ = '=';
    }
}

/**
 * A wrapper around std::array, representing a NULL-terminated string.
 * @tparam N The number of characters in the string (excluding the NULL-terminator)
 */
template<size_t N>
class static_string {
public:
    static constexpr size_t LENGTH{N};

    static_string() = default;

    explicit static_string(const std::array<char, N + 1>& data) : data_{data} {}

    [[nodiscard]] constexpr auto begin() const noexcept { return data_.cbegin(); }

    [[nodiscard]] constexpr auto end() const noexcept { return std::prev(data_.cend()); }

    [[nodiscard]] constexpr auto size() const noexcept { return N; }

    [[nodiscard]] constexpr auto length() const noexcept { return N; }

    [[nodiscard]] constexpr auto c_str() const noexcept { return data_.data(); }

    [[nodiscard]] constexpr auto data() const noexcept { return data_.data(); }

    [[nodiscard]] auto to_string() const { return std::string{begin(), end()}; }

private:
    std::array<char, N + 1> data_{};
};

static constexpr size_t TRUNCATED_DIGEST_SIZE{30};
using hash_t = std::array<uint8_t, TRUNCATED_DIGEST_SIZE>;
using base64_hash_t = static_string<(TRUNCATED_DIGEST_SIZE / 3) * 4 - 1>;

/**
 * Computes a truncated hash (30 bytes) of the input string and returns the binary digest array.
 */
[[nodiscard]] hash_t hash(Slice slice) {
    auto full_hash = std::array<uint8_t, BLAKE2S_OUTBYTES>{};
    // invariant full_hash.size() = BLAKE2S_OUTBYTES;
    // hashing never fails since bounds are derived from containers and output container size is BLAKE2S_OUTBYTES
    blake2s(full_hash.data(), full_hash.size(), slice.data, slice.size, nullptr, 0);

    auto truncated_hash = std::array<uint8_t, TRUNCATED_DIGEST_SIZE>{};

    static_assert(TRUNCATED_DIGEST_SIZE <= BLAKE2S_OUTBYTES);
    std::copy(full_hash.begin(), full_hash.begin() + TRUNCATED_DIGEST_SIZE, truncated_hash.begin());

    return truncated_hash;
}

/**
 * Computes the truncated hash (30 bytes) of the input string, encodes the result with base64 (resulting in
 * 40 bytes of base64), which is then NULL-terminated, returning a string of length 39 bytes and a NULL-terminator.
 */
[[nodiscard]] base64_hash_t hash_base64(Slice slice) {
    static_assert(TRUNCATED_DIGEST_SIZE % 3 == 0, "Digest must be divisible by 3 to avoid base64 padding.");
    static constexpr size_t BASE64_SIZE{(TRUNCATED_DIGEST_SIZE / 3) * 4};

    const auto digest{hash(slice)};
    std::array<char, BASE64_SIZE> readable_digest{};
    base64_encode(digest.data(), digest.size(), readable_digest.data());
    readable_digest.back() = '\0';

    return base64_hash_t{readable_digest};
}

}

StatusOr<ColumnPtr> CelonisStringhash::stringhash([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_VARCHAR> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto hash_str = hash_base64(input_string_viewer.value(row));
        result.append(Slice(hash_str.data(), hash_str.size()));
    }
    return result.build(all_const);
}

} // namespace starrocks
