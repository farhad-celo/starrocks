#pragma once

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "variant.h"
#include "variant_agg.h"
#include <boost/functional/hash.hpp>

namespace starrocks {

class ClusterStringsState {
public:
    ClusterStringsState() = default;

    ~ClusterStringsState() = default;

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        // _const_columns in merge is not aligned with _arg_types. So we pass all consts from
        // update() through serialization.
        if (ctx->is_notnull_constant_column(2)) {
            edit_threshold_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
        }
        if (ctx->is_notnull_constant_column(3)) {
            weighted_tokens_ = ColumnHelper::get_const_value<TYPE_VARCHAR>(ctx->get_constant_column(3));
        }
        if (ctx->is_notnull_constant_column(4)) {
            token_weight_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(4));
        }
        if (token_weight_ < 0) {
            std::string msg = "TOKEN_WEIGHT must be non-negative, however it is " + std::to_string(token_weight_);
            ctx->set_error(msg.c_str(), false);
            return;
        }
        // Expects columns: [0] string_column, [1] hash_column
        const Column* string_column = columns[0];
        if (string_column->is_nullable()) {
            const NullableColumn* nullable_string_column = down_cast<const NullableColumn*>(string_column);
            string_column = nullable_string_column->data_column().get();
        }
        std::optional<std::string> string_value = std::nullopt;
        if (!columns[0]->is_null(row_num)) {
            const BinaryColumn& strings = *(down_cast<const BinaryColumn*>(
                    ColumnHelper::get_data_column(string_column)));
            string_value = strings.get(row_num).get_slice().to_string();
        }

        const Column* hash_column = columns[1];
        if (hash_column->is_nullable()) {
            const NullableColumn* nullable_hash_column = down_cast<const NullableColumn*>(hash_column);
            hash_column = nullable_hash_column->data_column().get();
        }
        if (columns[1]->is_null(row_num)) {
            ctx->set_error(std::string("hash column should not contain NULL").c_str(), false);
            return;
        }
        const Int128Column& hashes = *(down_cast<const Int128Column*>(ColumnHelper::get_data_column(hash_column)));
        int128_t hash128 = hashes.get(row_num).get_int128();
        if (string_value.has_value()) {
            auto& string_with_count = hash_to_string_with_count_[hash128];
            string_with_count.first = string_value.value();
            string_with_count.second += 1;
        } else {
            null_hashes_.insert(hash128);
        }
    }

    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(int64_t);                       // edit_threshold_
        result += sizeof(int64_t);                       // token_weight_
        result += weighted_tokens_.size() + 1;           // weighted_tokens_
        result += sizeof(uint32_t);                      // num_null_hashes
        result += sizeof(int128_t) * null_hashes_.size();

        result += sizeof(uint32_t); // num_hash_to_string_with_count
        for (const auto& [hash128, string_count]: hash_to_string_with_count_) {
            result += sizeof(int128_t);                 // hash
            result += string_count.first.size() + 1;    // string
            result += sizeof(int64_t);                  // count
        }
        return result;
    };

    void serialize(uint8_t* dst) const {
        // serialization format:
        // edit_threshold_
        // token_weight_
        // weighted_tokens_ (str)
        // num_null_hashes
        // null_hash1, null_hash2, ...
        // num_hash_to_string_with_count
        // (hash, string, count), (hash, string, count)
        memcpy(dst, &edit_threshold_, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, &token_weight_, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, weighted_tokens_.data(), weighted_tokens_.size() + 1);
        dst += weighted_tokens_.size() + 1;
        const uint32_t num_null_hashes = null_hashes_.size();
        memcpy(dst, &num_null_hashes, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (int128_t hash128: null_hashes_) {
            memcpy(dst, &hash128, sizeof(int128_t));
            dst += sizeof(int128_t);
        }
        const uint32_t num_hash_to_strings = hash_to_string_with_count_.size();
        memcpy(dst, &num_hash_to_strings, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (const auto& [hash128, string_count]: hash_to_string_with_count_) {
            memcpy(dst, &hash128, sizeof(int128_t));
            dst += sizeof(int128_t);
            memcpy(dst, string_count.first.data(), string_count.first.size() + 1);
            dst += string_count.first.size() + 1;
            memcpy(dst, &string_count.second, sizeof(int64_t));
            dst += sizeof(int64_t);
        }
    }

    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src) {
        // read from src and merge with existing state.
        memcpy(&edit_threshold_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        memcpy(&token_weight_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        weighted_tokens_ = std::string(reinterpret_cast<const char*>(src));
        src += weighted_tokens_.size() + 1;
        uint32_t num_null_hashes;
        memcpy(&num_null_hashes, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        for (auto i = 0; i < num_null_hashes; ++i) {
            int128_t hash128;
            memcpy(&hash128, src, sizeof(int128_t));
            null_hashes_.insert(hash128);
            src += sizeof(int128_t);
        }
        uint32_t num_hash_to_strings;
        memcpy(&num_hash_to_strings, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        // (hash, string, count), (hash, string, count)
        for (auto i = 0; i < num_hash_to_strings; ++i) {
            int128_t hash128;
            memcpy(&hash128, src, sizeof(int128_t));
            src += sizeof(int128_t);
            std::string string_value = std::string(reinterpret_cast<const char*>(src));
            src += string_value.size() + 1;
            int64_t count;
            memcpy(&count, src, sizeof(int64_t));
            src += sizeof(int64_t);
            auto& string_with_count = hash_to_string_with_count_[hash128];
            string_with_count.first = string_value;
            string_with_count.second += count;
        }
        return 0;
    }

    int64_t edit_threshold() const { return edit_threshold_; }

    int64_t token_weight() const { return token_weight_; }

    const std::string& weighted_tokens() const { return weighted_tokens_; }

    const HashSet<int128_t>& null_hashes() const { return null_hashes_; }

    const phmap::flat_hash_map<int128_t, std::pair<std::string, int64_t>, StdHash<int128_t>>&
    hash_to_string_with_count() const {
        return hash_to_string_with_count_;
    }

private:
    std::string weighted_tokens_;
    int64_t edit_threshold_ = 0;
    int64_t token_weight_ = 1;
    HashSet<int128_t> null_hashes_;
    phmap::flat_hash_map<int128_t, std::pair<std::string, int64_t>, StdHash<int128_t>> hash_to_string_with_count_;
};

/**
 * @param: [ string_column, hash_column, EDIT_THRESHOLD, WEIGHTED_TOKENS, TOKEN_WEIGHT ]
 * @paramType columns: [ VARCHAR, LARGEINT, BIGINT, VARCHAR, BIGINT]
 * @return: STRUCT(hash: ARRAY_LARGEINT, cluster_representative: ARRAY_VARCHAR)
 * string_column : input string column.
 * hash_column: 128 bits hash of the string.
 * EDIT_THRESHOLD: The threshold which is used to define the cluster. If the edit distance between s1 and s2 is <=
 *                 EDIT_THRESHOLD, s1 and s2 are in the same cluster.
 * WEIGHTED_TOKENS: String of tokens for which an edit operation should have a user defined cost.
 * TOKEN_WEIGHT: cost of the weighted tokens. It must be >= 0. For the other tokens, the default weight is 1.
 *
 * cost(ch) = TOKEN_WEIGHT if ch in WEIGHTED_TOKENS else 1
 * 1. Insertion cost of ch: cost(ch);
 * 2. Deletion cost of ch: cost(ch);
 * 3. Cost of replacing ch1 with ch2: max(cost(ch1), cost(ch2)).
 *
 * Clustering is transitive: if s1 and s2 are similar, s2 and s3 are similar, then s1, s2, s3 will end up in the same
 * cluster.
 *
 * Used to support PQL CLUSTER_VARIANTS
 * https://docs.celonis.com/en/cluster_strings.html
 */
class ClusterStringsAggregateFunction
        : public AggregateFunctionBatchHelper<ClusterStringsState, ClusterStringsAggregateFunction> {
public:

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    std::string get_name() const override;
};

} // namespace starrocks
