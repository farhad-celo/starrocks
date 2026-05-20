#pragma once

#include <boost/multiprecision/cpp_bin_float.hpp>
#include <set>

#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"

namespace starrocks {

struct CelonisCalcStringBucketCountBoundariesAggregateState {
    // This function assumes the row_num row of string_column and hash_column does not contain NULL.
    void update(const Column* string_column, const Column* hash_column, size_t row_num) {
        auto string_value = string_column->get(row_num).get_slice().to_string();
        if (!min_max_set) {
            min_string = string_value;
            max_string = string_value;
            min_max_set = true;
        } else {
            if (string_value < min_string) {
                min_string = string_value;
            }
            if (string_value > max_string) {
                max_string = string_value;
            }
        }
        if (sample_ratio == 1.0) {
            strings.insert(string_value);
        } else {
            int128_t hash128 = hash_column->get(row_num).get<int128_t>();
            if (safe_abs(hash128) <
                boost::multiprecision::cpp_bin_float_quad(sample_ratio) * std::numeric_limits<int128_t>::max()) {
                strings.insert(string_value);
            }
        }
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint32_t); // size of strings
        for (const auto& str : strings) {
            result += str.size() + 1;
        }
        result += sizeof(double);        // sample_ratio
        result += sizeof(int64_t);       // count
        result += sizeof(uint8_t);       // min_max_set
        result += min_string.size() + 1; // min_string
        result += max_string.size() + 1; // max_string
        return result;
    }

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    void serialize(uint8_t* dst) const {
        uint32_t num_strings = strings.size();
        memcpy(dst, &num_strings, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        // asc order
        for (auto it = strings.begin(); it != strings.end(); ++it) {
            const auto& str = *it;
            memcpy(dst, str.data(), str.size() + 1);
            dst += str.size() + 1;
        }
        memcpy(dst, &sample_ratio, sizeof(double));
        dst += sizeof(double);
        memcpy(dst, &count, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, &min_max_set, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        memcpy(dst, min_string.data(), min_string.size() + 1);
        dst += min_string.size() + 1;
        memcpy(dst, max_string.data(), max_string.size() + 1);
        dst += max_string.size() + 1;
    }

    // Deserializes a CelonisCalcStringBucketCountBoundariesAggregateState object and merges it with the current state.
    void deserialize_and_merge(const uint8_t* src, size_t len) {
        const uint8_t* end = src + len;
        uint32_t num_strings;
        memcpy(&num_strings, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        auto hint = strings.begin();
        for (auto i = 0; i < num_strings; ++i) {
            const std::string str = std::string(reinterpret_cast<const char*>(src));
            src += str.size() + 1;
            hint = strings.insert(hint, str);
            ++hint;
        }
        memcpy(&sample_ratio, src, sizeof(double));
        src += sizeof(double);
        memcpy(&count, src, sizeof(int64_t));
        src += sizeof(int64_t);
        bool cur_min_max_set;
        memcpy(&cur_min_max_set, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        const std::string cur_min_string = std::string(reinterpret_cast<const char*>(src));
        src += cur_min_string.size() + 1;
        const std::string cur_max_string = std::string(reinterpret_cast<const char*>(src));
        src += cur_max_string.size() + 1;
        if (cur_min_max_set) {
            if (!min_max_set) {
                min_string = cur_min_string;
                max_string = cur_max_string;
                min_max_set = true;
            } else {
                if (cur_min_string < min_string) {
                    min_string = cur_min_string;
                }
                if (cur_max_string > max_string) {
                    max_string = cur_max_string;
                }
            }
        }
        DCHECK_EQ(src, end);
        initialized = true;
    }

    bool initialized = false;
    std::set<std::string> strings;
    double sample_ratio = 1.0;
    int64_t count = 10;
    bool min_max_set = false;
    std::string min_string = "";
    std::string max_string = "";
};

/**
 * @param: [string_col, hash_col, count, sample_ratio]
 * @paramType: [VARCHAR, LARGEINT, CONST BIGINT, CONST DOUBLE]
 * @return: ARRAY_VARCHAR
 * This is the string version of CELONIS_CALC_BUCKET_COUNT_BOUNDARIES.
 *
 */
class CelonisCalcStringBucketCountBoundariesAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisCalcStringBucketCountBoundariesAggregateState,
                                              CelonisCalcStringBucketCountBoundariesAggregationFunction> {
public:
    void create_impl(FunctionContext* ctx, const Column** columns,
                     CelonisCalcStringBucketCountBoundariesAggregateState& state) const {
        DCHECK_EQ(ctx->get_num_args(), 4);
        state.initialized = true;
        auto count = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
        if (count > 0) {
            state.count = count;
        }
        auto sample_ratio = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx->get_constant_column(3));
        if (!is_ratio_invalid(sample_ratio)) {
            state.sample_ratio = sample_ratio;
        }
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            create_impl(ctx, columns, state_impl);
        }
        // Ignore when string or hash is NULL
        if ((columns[0]->is_nullable() && columns[0]->is_null(row_num)) ||
            (columns[1]->is_nullable() && columns[1]->is_null(row_num))) {
            return;
        }
        state_impl.update(columns[0], columns[1], row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        // merge internal state with column[row_num]
        // the column type is binary
        if (column->is_nullable() && column->is_null(row_num)) {
            return;
        }
        const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
        Slice slice = input_column->get_slice(row_num);
        this->data(state).deserialize_and_merge((const uint8_t*)slice.data, slice.size);
    }

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                             Column* to) const override {
        // append our serialized state to column "to"
        auto* column = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(to));
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        size_t old_size = column->get_bytes().size();
        size_t new_size = old_size + this->data(state).serialized_size();
        column->get_bytes().resize(new_size);
        this->data(state).serialize(column->get_bytes().data() + old_size);
        column->get_offset().emplace_back(new_size);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        // Used for streaming aggregation passthrough. Not implemented.
        throw std::runtime_error(
                "celonis_calc_string_bucket_count_boundaries: convert_to_serialize_format not supported");
    }

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override {
        auto& state_impl = this->data(state);
        auto* data_column = to;
        NullData* null_data = nullptr;
        if (to->is_nullable()) {
            auto* nullable_column = down_cast<NullableColumn*>(to);
            null_data = &nullable_column->null_column_data();
            if (!state_impl.initialized) {
                nullable_column->append_default();
                return;
            }
            data_column = nullable_column->mutable_data_column();
        } else if (!state_impl.initialized) {
            to->append_default();
            return;
        }

        std::set<std::string> string_set = state_impl.strings;
        if (state_impl.min_max_set) {
            string_set.insert(state_impl.min_string);
            string_set.insert(state_impl.max_string);
        }
        const auto n = state_impl.count;
        if (n > MAX_NUM_BUCKETS) {
            ctx->set_error(std::string("The number of buckets is more than " + std::to_string(MAX_NUM_BUCKETS)).c_str(),
                           false);
            return;
        }
        // The output column is nullable, populate null_data.
        if (null_data != nullptr) {
            null_data->push_back(0);
        }
        std::vector<std::string> boundaries = compute_boundaries(string_set, n);
        // populate output column
        ArrayColumn* array_column = down_cast<ArrayColumn*>(data_column);
        auto* elements_column = array_column->elements_column().get();
        auto* offsets_column = array_column->offsets_column().get();
        int n_elements = 0;
        for (const auto& boundary : boundaries) {
            elements_column->append_datum(Slice(boundary));
            ++n_elements;
        }
        int new_offset = offsets_column->get_data().back() + n_elements;
        offsets_column->get_data().push_back(new_offset);
    }

    std::string get_name() const override { return "celonis_calc_string_bucket_count_boundaries"; }

private:
    std::vector<std::string> compute_boundaries(const std::set<std::string>& string_set, int64_t n) const {
        std::vector<std::string> boundaries;
        if (string_set.empty() || n <= 0) {
            return boundaries;
        }
        size_t num_strings = string_set.size();
        size_t bucket_size = num_strings / n;
        auto reminder = num_strings % n;

        std::multiset<std::string>::const_iterator it = string_set.begin();
        boundaries.push_back(*it);
        size_t cnt = 0;
        auto current_bucket_size = bucket_size + (reminder > 0 ? 1 : 0);
        if (reminder > 0) {
            --reminder;
        }
        for (; it != string_set.end(); ++it) {
            if (cnt == current_bucket_size) {
                boundaries.push_back(*it);
                cnt = 0;
                current_bucket_size = bucket_size + (reminder > 0 ? 1 : 0);
                if (reminder > 0) {
                    --reminder;
                }
            }
            cnt += 1;
        }
        return boundaries;
    }
};

} // namespace starrocks
