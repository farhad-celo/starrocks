#pragma once

#include <boost/algorithm/string/join.hpp>
#include <execution>

#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

template <LogicalType LT, typename T = RunTimeCppType<LT>>
std::string to_model(const std::vector<std::pair<std::optional<T>, std::optional<T>>>& ranges,
                     const std::map<T, std::vector<size_t>>& num_to_counter) {
    // create the model
    std::string model;
    // Use [-1, 0] to represent empty range.
    std::vector<std::string> value_strs;
    for (int i = 1; i <= 3; ++i) {
        if (ranges[i].first.has_value()) {
            value_strs.push_back(std::to_string(ranges[i].first.value()));
        } else {
            value_strs.push_back(std::to_string(T{}));
        }
        if (ranges[i].second.has_value()) {
            value_strs.push_back(std::to_string(ranges[i].second.value()));
        } else {
            value_strs.push_back(std::to_string(T{} - 1));
        }
    }
    model += boost::algorithm::join(value_strs, ",");
    model += ":";
    std::vector<std::string> num_section_strs;
    for (const auto& [num, counter] : num_to_counter) {
        std::vector<std::string> items;
        items.push_back(std::to_string(num));
        size_t total = std::accumulate(counter.begin(), counter.end(), static_cast<size_t>(0));
        items.push_back(std::to_string(static_cast<double>(counter[1]) / total));
        items.push_back(std::to_string(static_cast<double>(counter[2]) / total));
        items.push_back(std::to_string(static_cast<double>(counter[3]) / total));
        num_section_strs.push_back(boost::algorithm::join(items, ","));
    }
    model += boost::algorithm::join(num_section_strs, ";");
    return model;
}

template <LogicalType LT>
struct CelonisAbcModelAggregateState {
    using CppType = RunTimeCppType<LT>;

    void update(const Column* value_column, const Column* pk_hash_column, size_t row_num) {
        auto num = value_column->get(row_num).get<RunTimeCppType<LT>>();
        if (sample_ratio == 1.0) {
            nums[num]++;
        } else {
            int64_t pk_hash = pk_hash_column->get(row_num).get<int64_t>();
            double prob =
                    static_cast<double>(safe_abs(pk_hash)) / static_cast<double>(std::numeric_limits<int64_t>::max());
            if (prob < sample_ratio) {
                nums[num]++;
            }
        }
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint32_t);                                 // size of nums
        result += (sizeof(CppType) + sizeof(size_t)) * nums.size(); // key-value pairs in nums
        result += sizeof(double) * 3;                               // sample_ratio, ratio_a, ratio_b
        return result;
    }

    // Writes a binary encoded version of the object to dst.
    // The size written will be serialized_size()
    // As of 2024-03-01, SR drops array literal in merge and _const_columns in merge is not aligned with
    // _arg_types. So we pass all consts from update() through serialization.
    void serialize(uint8_t* dst) const {
        uint32_t nums_size = nums.size();
        memcpy(dst, &nums_size, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (const auto& [num, count] : nums) {
            memcpy(dst, &num, sizeof(CppType));
            dst += sizeof(CppType);
            memcpy(dst, &count, sizeof(size_t));
            dst += sizeof(size_t);
        }
        memcpy(dst, &sample_ratio, sizeof(double));
        dst += sizeof(double);
        memcpy(dst, &ratio_a, sizeof(double));
        dst += sizeof(double);
        memcpy(dst, &ratio_b, sizeof(double));
        dst += sizeof(double);
    }

    // Deserializes a CelonisAbcModelAggregateState object and merges it with the current state.
    void deserialize_and_merge(const uint8_t* src, size_t len) {
        const uint8_t* end = src + len;
        uint32_t num_size;
        memcpy(&num_size, src, sizeof(uint32_t));
        src += sizeof(uint32_t);

        for (auto i = 0; i < num_size; ++i) {
            CppType num;
            size_t count;
            memcpy(&num, src, sizeof(CppType));
            src += sizeof(CppType);
            memcpy(&count, src, sizeof(size_t));
            src += sizeof(size_t);
            nums[num] += count;
        }
        double sample_r;
        memcpy(&sample_r, src, sizeof(double));
        src += sizeof(double);
        sample_ratio = sample_r;
        double ra, rb;
        memcpy(&ra, src, sizeof(double));
        src += sizeof(double);
        ratio_a = ra;
        memcpy(&rb, src, sizeof(double));
        src += sizeof(double);
        ratio_b = rb;
        DCHECK_EQ(src, end);
        initialized = true;
    }

    bool initialized = false;
    double ratio_a = 0.8;
    double ratio_b = 0.15;
    double sample_ratio = 1.0;
    phmap::flat_hash_map<CppType, size_t, StdHash<CppType>> nums;
};

/**
 * @param: [value_col, pk_hash_col, sample_ratio, A, B]
 * @paramType: [BIGINT | DOUBLE, BIGINT, CONST DOUBLE, CONST DOUBLE, const DOUBLE]
 * @return: VARCHAR
 *
 */
template <LogicalType LT, typename T = RunTimeCppType<LT>>
class CelonisAbcModelAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisAbcModelAggregateState<LT>,
                                              CelonisAbcModelAggregationFunction<LT, T>> {
public:
    using ColumnType = RunTimeColumnType<LT>;

    void create_impl(FunctionContext* ctx, const Column** columns, CelonisAbcModelAggregateState<LT>& state) const {
        DCHECK_EQ(ctx->get_num_args(), 5);
        state.initialized = true;
        state.sample_ratio = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx->get_constant_column(2));
        state.ratio_a = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx->get_constant_column(3));
        state.ratio_b = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx->get_constant_column(4));
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            create_impl(ctx, columns, state_impl);
        }
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
        throw std::runtime_error("celonis_build_abc_model: convert_to_serialize_format not supported");
    }

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override {
        if (UNLIKELY(ctx->state()->cancelled_ref())) {
            ctx->set_error("celonis_build_abc_model detects cancelled.", false);
            return;
        }
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            to->append_default();
            return;
        }
        const double ratio_c = 1.0 - state_impl.ratio_a - state_impl.ratio_b;
        if (is_ratio_invalid(state_impl.sample_ratio) || is_ratio_invalid(state_impl.ratio_a) ||
            is_ratio_invalid(state_impl.ratio_b) || is_ratio_invalid(ratio_c)) {
            to->append_nulls(1);
            return;
        }

        // Calculate total sum considering counts
        double total_sum = 0.0;
        for (const auto& [num, count] : state_impl.nums) {
            total_sum += static_cast<double>(num) * static_cast<double>(count);
        }

        const double a_breakpoint = static_cast<double>(total_sum) * state_impl.ratio_a;
        const double b_breakpoint = static_cast<double>(total_sum) * (state_impl.ratio_a + state_impl.ratio_b);
        std::vector<std::pair<std::optional<T>, std::optional<T>>> ranges;
        ranges.resize(4);
        double cur_sum = 0.0;
        std::map<T, std::vector<size_t>> num_to_counter;

        // Need to sort keys to traverse from high to low
        std::vector<std::pair<T, size_t>> sorted_nums(state_impl.nums.begin(), state_impl.nums.end());
        std::sort(std::execution::par_unseq, sorted_nums.begin(), sorted_nums.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::optional<T> pre_num;
        std::vector<size_t> counter(4, 0);

        // traverse number from high to low
        for (const auto& [num, count] : sorted_nums) {
            for (auto i = 0; i < count; ++i) {
                cur_sum += static_cast<double>(num);
                int category = 3;
                if (cur_sum <= a_breakpoint) {
                    category = 1;
                } else if (cur_sum <= b_breakpoint) {
                    category = 2;
                }
                // set high
                if (!ranges[category].second.has_value()) {
                    ranges[category].second = num;
                }
                // set low
                ranges[category].first = num;

                if (pre_num.has_value() && num != pre_num.value()) {
                    // pre_num appears in multiple groups
                    if (std::count_if(counter.begin(), counter.end(), [](int x) { return x > 0; }) > 1) {
                        num_to_counter.insert({pre_num.value(), counter});
                    }
                    counter = std::vector<size_t>(4, 0);
                }
                counter[category] += 1;
                pre_num = num;
            }
        }
        if (pre_num.has_value() && std::count_if(counter.begin(), counter.end(), [](int x) { return x > 0; }) > 1) {
            num_to_counter.insert({pre_num.value(), counter});
        }
        std::string model = to_model<LT>(ranges, num_to_counter);
        to->append_datum(model.c_str());
    }

    std::string get_name() const override { return "celonis_build_abc_model"; }
};

} // namespace starrocks
