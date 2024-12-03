#pragma once

#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/lu.hpp>

namespace starrocks {

namespace {

using namespace boost::numeric::ublas;

// precision used when converting doubles to string during generating model string.
static const int PRECISION = 17;

// Performs in-place LU factorization and then solve for X in AX=B
bool lu_solve(const matrix<double>& A, const vector<double>& B, vector<double>& X) {
    matrix<double> A_lu(A);
    permutation_matrix<std::size_t> perm(A.size1());
    int res = lu_factorize(A_lu, perm);
    if (res != 0) return false;
    X.assign(B);
    lu_substitute(A_lu, perm, X);
    return true;
}

std::string to_model_str(const vector<double>& beta) {
    std::vector<std::string> beta_strs;
    for (double v: beta) {
        beta_strs.push_back(double_to_string(v, PRECISION));
    }
    return boost::algorithm::join(beta_strs, ":");
}

template<LogicalType LT>
struct CelonisMultiLinearRegressionModelAggregateState {
    using CppType = RunTimeCppType<LT>;

    void initialize(int64_t num_features) {
        n_features = num_features;
        sum_x.resize(n_features, CppType{});
        sum_xy.resize(n_features, CppType{});
        sum_xx.resize(n_features * n_features, CppType{});
        n_samples = 0;
        sum_y = CppType{};
        initialized = true;
    }

    void update(const Column* x_column, const Column* y_column, size_t row_num) {
        auto array = x_column->get(row_num).get_array();
        if (n_features <= 0) {
            initialize(array.size());
        }
        DCHECK_EQ(array.size(), n_features);
        auto y = y_column->get(row_num).get<CppType>();
        std::vector<CppType> x;
        for (auto i = 0; i < array.size(); ++i) {
            x.push_back(array[i].get<CppType>());
        }
        sum_y += y;
        for (auto i = 0; i < n_features; ++i) {
            sum_x[i] += x[i];
            sum_xy[i] += x[i] * y;
        }
        // (n_features X 1) * (1 X n_features)
        for (auto i = 0; i < n_features; ++i) {
            for (auto j = 0; j < n_features; ++j) {
                sum_xx[i * n_features + j] += x[i] * x[j];
            }
        }
        ++n_samples;
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(int64_t);                    // n_samples
        result += sizeof(int64_t);                    // n_features;
        result += sizeof(CppType) * sum_x.size();     // items in sum_x
        result += sizeof(CppType) * sum_xx.size();    // items in sum_xx
        result += sizeof(CppType) * sum_xy.size();    // items in sum_xy
        result += sizeof(CppType);                    // sum_y
        return result;
    }

    void serialize(const std::vector<CppType>& vec, uint8_t** dst) const {
        for (CppType num: vec) {
            memcpy(*dst, &num, sizeof(CppType));
            *dst += sizeof(CppType);
        }
    }

    void serialize(int64_t x, uint8_t** dst) const {
        memcpy(*dst, &x, sizeof(int64_t));
        *dst += sizeof(int64_t);
    }

    int64_t deserialize(const uint8_t** src) {
        int64_t x;
        memcpy(&x, *src, sizeof(int64_t));
        *src += sizeof(int64_t);
        return x;
    }

    std::vector<CppType> deserialize(const uint8_t** src, uint32_t len) {
        std::vector<CppType> vec;
        for (auto i = 0; i < len; ++i) {
            CppType num;
            memcpy(&num, *src, sizeof(CppType));
            vec.push_back(num);
            *src += sizeof(CppType);
        }
        return vec;
    }

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    // As of 2024-03-01, SR drops array literal in merge and _const_columns in merge is not aligned with
    // _arg_types. So we pass all consts from update() through serialization.
    void serialize(uint8_t* dst) const {
        serialize(n_samples, &dst);
        serialize(n_features, &dst);
        serialize(sum_x, &dst);
        serialize(sum_xx, &dst);
        serialize(sum_xy, &dst);
        memcpy(dst, &sum_y, sizeof(CppType));
        dst += sizeof(CppType);
    }

    void update_vector(const std::vector<CppType>& new_vec, std::vector<CppType>& vec) {
        DCHECK_EQ(new_vec.size(), vec.size());
        for (auto i = 0; i < vec.size(); ++i) {
            vec[i] += new_vec[i];
        }
    }

    // Deserializes a CelonisMultiLinearRegressionModelAggregateState object and merges it with the current state.
    void deserialize_and_merge(const uint8_t* src, size_t len) {
        const uint8_t* end = src + len;
        int64_t new_n_samples = deserialize(&src);
        int64_t new_n_features = deserialize(&src);
        if (new_n_features <= 0) {
            return;
        }
        if (n_features <= 0) {
            initialize(new_n_features);
        }
        DCHECK_EQ(new_n_features, n_features);
        n_samples += new_n_samples;
        auto new_sum_x = deserialize(&src, new_n_features);
        auto new_sum_xx = deserialize(&src, new_n_features * new_n_features);
        auto new_sum_xy = deserialize(&src, new_n_features);
        CppType new_sum_y;
        memcpy(&new_sum_y, src, sizeof(CppType));
        src += sizeof(CppType);
        update_vector(new_sum_x, sum_x);
        update_vector(new_sum_xx, sum_xx);
        update_vector(new_sum_xy, sum_xy);
        sum_y += new_sum_y;
        DCHECK_EQ(src, end);
        initialized = true;
    }

    bool initialized = false;
    int64_t n_samples = 0;
    int64_t n_features = -1;
    std::vector<CppType> sum_x;
    std::vector<CppType> sum_xx;
    std::vector<CppType> sum_xy;
    CppType sum_y = CppType{};
};

} // namespace

/**
 * @param: [x_col, y_col]
 * @paramType: [ARRAY_BIGINT | ARRAY_DOUBLE, BIGINT | DOUBLE]
 * @return: VARCHAR
 *
 */
template<LogicalType LT, typename T = RunTimeCppType<LT>>
class CelonisMultiLinearRegressionModelAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisMultiLinearRegressionModelAggregateState<LT>,
                CelonisMultiLinearRegressionModelAggregationFunction<LT, T>> {
public:
    using ColumnType = RunTimeColumnType<LT>;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& state_impl = this->data(state);
        if ((columns[0]->is_nullable() && columns[0]->is_null(row_num)) ||
            (columns[1]->is_nullable() && columns[1]->is_null(row_num))) {
            return;
        }
        auto array = columns[0]->get(row_num).get_array();
        for (auto i = 0; i < array.size(); ++i) {
            if (array[i].is_null()) {
                return;
            }
        }
        state_impl.update(columns[0], columns[1], row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        // merge internal state with column[row_num]
        // the column type is binary
        const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
        if (input_column->is_null(row_num)) {
            return;
        }
        Slice slice = input_column->get_slice(row_num);
        this->data(state).deserialize_and_merge((const uint8_t*) slice.data, slice.size);
    }

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
                "celonis_build_multi_linear_regression_model: convert_to_serialize_format not supported");
    }

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            to->append_default();
            return;
        }
        int64_t n_samples = state_impl.n_samples;
        int64_t n_features = state_impl.n_features;
        matrix<double> sum_xx_augmented(n_features + 1, n_features + 1);
        vector<double> sum_xy_augmented(n_features + 1);
        sum_xx_augmented(0, 0) = n_samples;
        for (auto j = 1; j <= n_features; ++j) {
            sum_xx_augmented(0, j) = state_impl.sum_x[j - 1];
        }
        for (auto i = 1; i <= n_features; ++i) {
            sum_xx_augmented(i, 0) = state_impl.sum_x[i - 1];
            for (auto j = 1; j <= n_features; ++j) {
                sum_xx_augmented(i, j) = state_impl.sum_xx[(i - 1) * n_features + j - 1];
            }
        }
        sum_xy_augmented(0) = state_impl.sum_y;
        for (auto i = 1; i <= n_features; ++i) {
            sum_xy_augmented(i) = state_impl.sum_xy[i - 1];
        }
        vector<double> beta(n_features + 1);
        if (lu_solve(sum_xx_augmented, sum_xy_augmented, beta)) {
            const std::string model = to_model_str(beta);
            to->append_datum(model.c_str());
        } else {
            to->append_datum(kNullDatum);
        }
    }

    std::string get_name() const override { return "celonis_build_multi_linear_regression_model"; }
};

} // namespace starrocks
