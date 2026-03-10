#include "exprs/celonis/agg/percentile_disc.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <cmath>
#include <random>

#include "../util.h"
#include "column/column_builder.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/celonis/agg/factory_calendar.h"
#include "exprs/celonis/agg/linear_regression.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

class CelonisPercentileDiscTest : public testing::Test {
public:
    CelonisPercentileDiscTest() = default;

    void SetUp() override {
        _allocator = std::make_unique<CountingAllocatorWithHook>();
        tls_agg_state_allocator = _allocator.get();
    }

    void TearDown() override {
        tls_agg_state_allocator = nullptr;
        _allocator.reset();
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*> RunUpdate(
            const DatumArray& data_array, double rate) {
        // auto local_ctx = get_ctx();
        const AggregateFunction* func =
                get_aggregate_function("celonis_percentile_disc", TYPE_DOUBLE, TYPE_DOUBLE, false);

        DCHECK(func != nullptr);

        FunctionContext::TypeDesc return_type =
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
        std::vector<FunctionContext::TypeDesc> arg_types;
        arg_types.push_back(
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE)));
        arg_types.push_back(
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE)));
        std::unique_ptr<FunctionContext> local_ctx{
                FunctionContext::create_test_context(std::move(arg_types), return_type)};

        auto rate_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(rate, data_array.size());
        local_ctx->set_constant_columns({rate_col});

        auto data_column = RunTimeColumnType<TYPE_DOUBLE>::create();
        for (const auto& d : data_array) {
            data_column->append_datum(d);
        }

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = data_column.get();
        raw_columns[1] = rate_col.get();

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), data_column->size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    void Run(const std::vector<DatumArray>& data_arrays, double rate, FixedLengthColumn<double>* result_col) {
        const auto n = data_arrays.size();
        std::vector<std::unique_ptr<FunctionContext>> ctxs;
        std::vector<std::unique_ptr<ManagedAggrState>> states;
        std::vector<const AggregateFunction*> funcs;

        ASSERT_TRUE(n > 1);
        ASSERT_TRUE(0. <= rate && rate <= 1.);

        ctxs.reserve(n);
        states.reserve(n);
        funcs.reserve(n);

        for (const auto& data_array : data_arrays) {
            auto [local_ctx, state, func] = RunUpdate(data_array, rate);
            ctxs.push_back(std::move(local_ctx));
            states.push_back(std::move(state));
            funcs.push_back(func);
        }

        std::vector<ColumnPtr> serialize_cols;
        auto func = funcs.at(0);
        serialize_cols.reserve(n - 1);
        for (auto i = 1UL; i < n; ++i) {
            ColumnPtr serialize_col = BinaryColumn::create();
            func->serialize_to_column(ctxs.at(i).get(), states.at(i)->state(), serialize_col.get());
            serialize_cols.push_back(std::move(serialize_col));
        }

        for (auto i = 0UL; i < n - 1; ++i) {
            func->merge(ctxs.at(0).get(), serialize_cols.at(i).get(), states.at(0)->state(), 0);
        }

        func->finalize_to_column(ctxs.at(0).get(), states.at(0)->state(), result_col);

        for (auto i = 0UL; i < n; ++i) {
            ASSERT_FALSE(ctxs.at(i)->has_error());
        }
    }

    template <typename T>
    static DatumArray generate_randomized_datum_array(std::size_t size, long min_val, long max_val, int seed = 0) {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> distr(min_val, max_val);

        DatumArray arr;
        arr.reserve(size);

        for (int i = 0; i < size; ++i) {
            const auto num = distr(gen);
            arr.push_back(static_cast<T>(num));
        }

        return arr;
    }

    template <LogicalType LT>
    decltype(auto) calculateExpectedResult(const std::vector<DatumArray>& grid, double rate) {
        static constexpr auto ResultLT = PercentileResultLT<LT, false>;
        using ResultType = RunTimeCppType<ResultLT>;
        using CppType = RunTimeCppType<LT>;

        std::vector<CppType> new_vector;
        for (auto& data_array : grid) {
            for (const auto& d : data_array) {
                new_vector.push_back(d.get_double());
            }
        }

        pdqsort(new_vector.begin(), new_vector.end());

        std::size_t index = std::floor(new_vector.size() * rate);
        index = std::max(0UL, std::min(index, static_cast<std::size_t>(new_vector.size()) - 1));

        ResultType result = new_vector[index];
        return result;
    }

private:
    std::unique_ptr<CountingAllocatorWithHook> _allocator;
};

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small1) {
    const auto data_array1 = DatumArray{102., 101., 100., 4., 3.};
    const auto data_array2 = DatumArray{2., 1., -100., -101., -102.};
    const std::vector data_arrays{data_array1, data_array2};
    constexpr double rate = 0.5;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col.get());
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small2) {
    const auto data_array1 = generate_randomized_datum_array<double>(10, -1'000, 1'000, 0);
    const auto data_array2 = generate_randomized_datum_array<double>(10, -1'000, 1'000, 1);
    const std::vector data_arrays{data_array1, data_array2};
    constexpr double rate = 0.5;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small3) {
    const auto data_array1 = generate_randomized_datum_array<double>(5, -1'000, 1'000, 2);
    const auto data_array2 = generate_randomized_datum_array<double>(3, -1'000, 1'000, 3);
    const auto data_array3 = generate_randomized_datum_array<double>(2, -1'000, 1'000, 4);
    const std::vector data_arrays{data_array1, data_array2, data_array3};
    constexpr double rate = 0.5;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small4) {
    const auto data_array1 = generate_randomized_datum_array<double>(5, -1'000, 1'000, 5);
    const auto data_array2 = generate_randomized_datum_array<double>(3, -1'000, 1'000, 6);
    const auto data_array3 = generate_randomized_datum_array<double>(2, -1'000, 1'000, 7);
    const std::vector data_arrays{data_array1, data_array2, data_array3};
    constexpr double rate = 0.5;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small4_rate_0) {
    const auto data_array1 = generate_randomized_datum_array<double>(5, -1'000, 1'000, 5);
    const auto data_array2 = generate_randomized_datum_array<double>(3, -1'000, 1'000, 6);
    const auto data_array3 = generate_randomized_datum_array<double>(2, -1'000, 1'000, 7);
    const std::vector data_arrays{data_array1, data_array2, data_array3};
    constexpr double rate = 0.0;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small4_rate_1) {
    const auto data_array1 = generate_randomized_datum_array<double>(5, -1'000, 1'000, 5);
    const auto data_array2 = generate_randomized_datum_array<double>(3, -1'000, 1'000, 6);
    const auto data_array3 = generate_randomized_datum_array<double>(2, -1'000, 1'000, 7);
    const std::vector data_arrays{data_array1, data_array2, data_array3};
    constexpr double rate = 1.0;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_small5_equal_elements) {
    const auto data_array1 = DatumArray{102., 101., 100., 2., 2.};
    const auto data_array2 = DatumArray{2., 1., -100., -101., -102.};
    const std::vector data_arrays{data_array1, data_array2};
    constexpr double rate = 0.5;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_large1) {
    const auto data_array1 = generate_randomized_datum_array<double>(456, -10'000, 10'000, 0);
    const auto data_array2 = generate_randomized_datum_array<double>(321, -10'000, 10'000, 1);
    const auto data_array3 = generate_randomized_datum_array<double>(222, -10'000, 10'000, 2);
    const auto data_array4 = generate_randomized_datum_array<double>(210, -10'000, 10'000, 3);
    const auto data_array5 = generate_randomized_datum_array<double>(678, -10'000, 10'000, 4);
    const std::vector data_arrays{data_array1, data_array2, data_array3, data_array4, data_array5};
    constexpr double rate = 0.7;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_large2_equal_elements) {
    const auto data_array1 = generate_randomized_datum_array<double>(456, -10, 10, 0);
    const auto data_array2 = generate_randomized_datum_array<double>(321, -10, 10, 1);
    const auto data_array3 = generate_randomized_datum_array<double>(222, -10, 10, 2);
    const auto data_array4 = generate_randomized_datum_array<double>(210, -10, 10, 3);
    const auto data_array5 = generate_randomized_datum_array<double>(678, -10, 10, 4);
    const std::vector data_arrays{data_array1, data_array2, data_array3, data_array4, data_array5};
    constexpr double rate = 0.1;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);
    ASSERT_EQ(result_col->size(), 1);

    const auto expected_result = calculateExpectedResult<TYPE_DOUBLE>(data_arrays, rate);
    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(expected_result, actual_result);
}

TEST_F(CelonisPercentileDiscTest, test_celonis_percentile_disc_empty_input) {
    const auto data_array1 = DatumArray{};
    const auto data_array2 = DatumArray{};
    const std::vector data_arrays{data_array1, data_array2};
    constexpr double rate = 0.2;

    const auto result_col = RunTimeColumnType<TYPE_DOUBLE>::create();
    Run(data_arrays, rate, result_col);

    ASSERT_EQ(result_col->size(), 1);

    auto* p = reinterpret_cast<const double*>(result_col->raw_data());
    const double actual_result = p[0];
    ASSERT_EQ(0, actual_result);
}
} // namespace
} // namespace starrocks