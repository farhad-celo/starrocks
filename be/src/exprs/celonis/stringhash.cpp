#include "exprs/celonis/stringhash.h"

#include <openssl/evp.h>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/match_pattern_util.h"
#include "exprs/function_context.h"

// The implementation is copied from Saola: cpm-query-engine/blob/01a65c8ebe07c10cd7fda76eb3be95c070aada79/query-engine/src/main/native/cpm-accelerator/modules/common/stringhasher.cpp
// blake2 has been replaced with OpenSSL's EVP_blake2s256, which produces byte-identical output.
// The EVP_MD_CTX is reused across rows within a single call to avoid the per-row alloc/init/free
// overhead of the EVP envelope (which dominates runtime for short inputs).

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
            *pos++ = BASE64_TABLE[(in[0] & 0x03) << 4]; // NOLINT(bugprone-misplaced-widening-cast)
            *pos++ = '=';
        } else {
            *pos++ = BASE64_TABLE[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *pos++ = BASE64_TABLE[(in[1] & 0x0f) << 2]; // NOLINT(bugprone-misplaced-widening-cast)
        }
        *pos++ = '=';
    }
}

/**
 * A wrapper around std::array, representing a NULL-terminated string.
 * @tparam N The number of characters in the string (excluding the NULL-terminator)
 */
template <size_t N>
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

static constexpr size_t BLAKE2S_DIGEST_SIZE{32};
static constexpr size_t TRUNCATED_DIGEST_SIZE{30};
using hash_t = std::array<uint8_t, TRUNCATED_DIGEST_SIZE>;
using base64_hash_t = static_string<(TRUNCATED_DIGEST_SIZE / 3) * 4 - 1>;

/**
 * Computes a truncated hash (30 bytes) of the input string and returns the binary digest array.
 * Uses a caller-provided EVP_MD_CTX so that the heap allocation and provider lookup cost is paid
 * once per batch rather than per row.
 */
[[nodiscard]] hash_t hash(EVP_MD_CTX* ctx, const EVP_MD* md, Slice slice) {
    auto full_hash = std::array<uint8_t, BLAKE2S_DIGEST_SIZE>{};
    unsigned int hash_len = 0;

    // EVP_blake2s256 is BLAKE2s with 32-byte output and no key, matching the
    // previous call: blake2s(out, 32, in, len, nullptr, 0).
    // EVP_DigestInit_ex implicitly resets the context, so it is safe to call
    // repeatedly on a reused ctx.
    [[maybe_unused]] int rc = EVP_DigestInit_ex(ctx, md, nullptr);
    DCHECK_EQ(rc, 1);
    rc = EVP_DigestUpdate(ctx, slice.data, slice.size);
    DCHECK_EQ(rc, 1);
    rc = EVP_DigestFinal_ex(ctx, full_hash.data(), &hash_len);
    DCHECK_EQ(rc, 1);
    DCHECK_EQ(hash_len, BLAKE2S_DIGEST_SIZE);

    auto truncated_hash = std::array<uint8_t, TRUNCATED_DIGEST_SIZE>{};

    static_assert(TRUNCATED_DIGEST_SIZE <= BLAKE2S_DIGEST_SIZE);
    std::copy(full_hash.begin(), full_hash.begin() + TRUNCATED_DIGEST_SIZE, truncated_hash.begin());

    return truncated_hash;
}

/**
 * Computes the truncated hash (30 bytes) of the input string, encodes the result with base64 (resulting in
 * 40 bytes of base64), which is then NULL-terminated, returning a string of length 39 bytes and a NULL-terminator.
 */
[[nodiscard]] base64_hash_t hash_base64(EVP_MD_CTX* ctx, const EVP_MD* md, Slice slice) {
    static_assert(TRUNCATED_DIGEST_SIZE % 3 == 0, "Digest must be divisible by 3 to avoid base64 padding.");
    static constexpr size_t BASE64_SIZE{(TRUNCATED_DIGEST_SIZE / 3) * 4};

    const auto digest{hash(ctx, md, slice)};
    std::array<char, BASE64_SIZE> readable_digest{};
    base64_encode(digest.data(), digest.size(), readable_digest.data());
    readable_digest.back() = '\0';

    return base64_hash_t{readable_digest};
}

/**
 * RAII wrapper for EVP_MD_CTX so we don't leak on early returns / exceptions.
 */
class EvpMdCtxGuard {
public:
    EvpMdCtxGuard() : ctx_(EVP_MD_CTX_new()) {}
    ~EvpMdCtxGuard() {
        if (ctx_ != nullptr) {
            EVP_MD_CTX_free(ctx_);
        }
    }
    EvpMdCtxGuard(const EvpMdCtxGuard&) = delete;
    EvpMdCtxGuard& operator=(const EvpMdCtxGuard&) = delete;

    [[nodiscard]] EVP_MD_CTX* get() const noexcept { return ctx_; }

private:
    EVP_MD_CTX* ctx_;
};

} // namespace

StatusOr<ColumnPtr> CelonisStringhash::stringhash([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_VARCHAR> result(num_rows);

    // Allocate the EVP_MD_CTX and resolve the algorithm pointer once per call,
    // then reuse them across all rows. This avoids the per-row malloc/free and
    // (in OpenSSL 3.x) per-row provider/property lookup that otherwise dominates
    // runtime for short inputs.
    EvpMdCtxGuard ctx_guard;
    EVP_MD_CTX* ctx = ctx_guard.get();
    DCHECK(ctx != nullptr);
    const EVP_MD* md = EVP_blake2s256();
    DCHECK(md != nullptr);

    for (size_t row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto hash_str = hash_base64(ctx, md, input_string_viewer.value(row));
        result.append(Slice(hash_str.data(), hash_str.size()));
    }
    return result.build(all_const);
}

} // namespace starrocks
