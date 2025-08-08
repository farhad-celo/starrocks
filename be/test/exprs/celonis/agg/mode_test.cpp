#include "exprs/celonis/agg/mode.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "column/column_builder.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "runtime/mem_pool.h"
#include "types/timestamp_value.h"
#include "util/slice.h"

namespace starrocks {

namespace {

/* Test utils */
template <typename T>
[[nodiscard]] constexpr LogicalType AS_LOGICAL_TYPE();

/* To enable simplified templated tests, always use cstring and cast to target type later (nullopt represents NULL) */
using ValueProxy = const char*;
using DatumProxy = std::optional<const ValueProxy>;
using DatumProxyArray = std::vector<DatumProxy>;

template <typename T>
using nullable_val_t = std::optional<const T>;
template <typename T>
using nullable_vec_t = std::vector<nullable_val_t<T>>;

/** returns the datum_proxy value transformed to a value of type T (or nullopt) */
template <typename T>
[[nodiscard]] nullable_val_t<T> transform_to(const DatumProxy& datum_proxy);
template <typename T>
[[nodiscard]] nullable_vec_t<T> transform_to(const DatumProxyArray& input_data);

/** Wraps context for a test function execution */
template <LogicalType LT>
class FunctionExecutionTestContext final {
public:
    FunctionExecutionTestContext(FunctionContext* ctx, const bool has_null)
            : func_{get_aggregate_function("celonis_mode", LT, LT, has_null)},
              managed_state_ptr_{ManagedAggrState::create(ctx, func_)},
              has_null_{has_null} {}
    FunctionExecutionTestContext(const FunctionExecutionTestContext&) = delete;
    FunctionExecutionTestContext& operator=(const FunctionExecutionTestContext&) = delete;
    FunctionExecutionTestContext(FunctionExecutionTestContext&&) = default;
    FunctionExecutionTestContext& operator=(FunctionExecutionTestContext&&) = delete;
    ~FunctionExecutionTestContext() = default;
    [[nodiscard]] const AggregateFunction* func() const { return func_; }
    [[nodiscard]] AggDataPtr raw_state() { return managed_state_ptr_->state(); }
    [[nodiscard]] ConstAggDataPtr raw_state() const { return managed_state_ptr_->state(); }
    using StateType = CelonisModeState<LT>;
    [[nodiscard]] const StateType& typed_state() const {
        // N.B: All of this is actually UB but this is already heavily exploited in SR e.g., in the
        // AggregateFunctionStateHelper::data(...) calls
        const auto unnest{[this] {
            using NestedStateType = NullableAggregateFunctionState<StateType, false>;
            return reinterpret_cast<const NestedStateType*>(raw_state())->nested_state();
        }};
        const ConstAggDataPtr raw_state_ptr{has_null_ ? unnest() : raw_state()};
        const auto* const typed_state_ptr{reinterpret_cast<const StateType*>(raw_state_ptr)};
        return *typed_state_ptr;
    }
    [[nodiscard]] bool has_null() const { return has_null_; }

private:
    class ManagedAggrState {
    public:
        ~ManagedAggrState() { _func->destroy(_ctx, _state); }

        static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
            return std::make_unique<ManagedAggrState>(ctx, func);
        }

        [[nodiscard]] AggDataPtr state() const { return _state; }

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

    const AggregateFunction* func_;
    std::unique_ptr<ManagedAggrState> managed_state_ptr_;
    bool has_null_;
};

struct empty_result {};
void verify_final_column_result(const ColumnPtr& finalized_result_column, empty_result expected_result);
template <typename T>
void verify_final_column_result(const ColumnPtr& finalized_result_column, nullable_val_t<T> expected_result);

} // namespace

/* Types to test for: BIGINT, DOUBLE, VARCHAR, DATETIME */
using types_to_test = testing::Types<std::int64_t, double, Slice, TimestampValue>;

template <typename T>
class CelonisModeAggTest : public testing::Test {
public:
    CelonisModeAggTest() = default;
    using ValueType = T;
    static constexpr auto LT{AS_LOGICAL_TYPE<ValueType>()};
    using TestContext = FunctionExecutionTestContext<LT>;

    [[nodiscard]] TestContext run_update(const nullable_vec_t<ValueType>& input_column_data) {
        auto [test_ctx, input_column]{prepare_function_execution_with_data(input_column_data)};
        std::vector<const Column*> raw_input_column_ptrs = {input_column.get()};
        // execute aggregate function: update
        test_ctx.func()->update_batch_single_state(func_ctx_.get(), input_column_data.size(),
                                                   raw_input_column_ptrs.data(), test_ctx.raw_state());
        return std::move(test_ctx);
    }

    [[nodiscard]] ColumnPtr run_serialize(const TestContext& test_ctx) {
        auto serialization_dst = [&test_ctx]() -> ColumnPtr {
            auto binary_col = BinaryColumn::create();
            if (test_ctx.has_null()) {
                return NullableColumn::create(binary_col, NullColumn::create(0, 0));
            }
            return binary_col;
        }();
        // execute aggregate function: serialize
        test_ctx.func()->serialize_to_column(func_ctx_.get(), test_ctx.raw_state(), serialization_dst.get());
        return serialization_dst;
    }

    /** Does not take a state as this function only updates a temporary state which is returned serialized */
    [[nodiscard]] ColumnPtr run_update_and_serialize(const nullable_vec_t<ValueType>& input_column_data) {
        const auto test_ctx{run_update(input_column_data)};
        return run_serialize(test_ctx);
    }

    /** Returns the deserialized state of the given column (by deserializing and merging with an empty state) */
    [[nodiscard]] TestContext run_deserialize(const ColumnPtr& deserialization_src) {
        const bool is_nullable(dynamic_cast<const NullableColumn*>(deserialization_src.get()) != nullptr);
        auto test_ctx{prepare_function_execution(is_nullable)};
        test_ctx.func()->merge(func_ctx_.get(), deserialization_src.get(), test_ctx.raw_state(), 0);
        return test_ctx;
    }

    void run_merge(const ColumnPtr& deserialization_src, TestContext& test_ctx) {
        test_ctx.func()->merge(func_ctx_.get(), deserialization_src.get(), test_ctx.raw_state(), 0);
    }

    [[nodiscard]] ColumnPtr run_finalize_to_column(const TestContext& test_ctx) {
        ColumnBuilder<LT> builder(config::vector_chunk_size);
        auto mut_output_column = test_ctx.has_null() ? builder.build_nullable_column() : builder.build(false);
        ColumnPtr output_column = std::move(mut_output_column);
        test_ctx.func()->finalize_to_column(func_ctx_.get(), test_ctx.raw_state(), output_column.get());
        return output_column;
    }

private:
    [[nodiscard]] TestContext prepare_function_execution(const bool has_null) {
        return TestContext{func_ctx_.get(), has_null};
    }
    [[nodiscard]] std::pair<TestContext, ColumnPtr> prepare_function_execution_with_data(
            const nullable_vec_t<ValueType>& input_column_data) {
        ColumnBuilder<LT> builder(config::vector_chunk_size);
        for (const auto& input_column_value : input_column_data) {
            if (input_column_value.has_value()) {
                builder.append(*input_column_value);
            } else {
                builder.append_null();
            }
        }
        auto mut_col_ptr = builder.build(/*is_const*/ false);
        ColumnPtr col_ptr = std::move(mut_col_ptr);
        return {prepare_function_execution(col_ptr->has_null()), col_ptr};
    }

    inline static auto type_desc{TypeDescriptor::from_logical_type(LT)};
    std::unique_ptr<MemPool> mem_pool_{std::make_unique<MemPool>()};
    std::unique_ptr<FunctionContext> func_ctx_{
            FunctionContext::create_context(/*state*/ nullptr, mem_pool_.get(), type_desc, {type_desc})};
};

TYPED_TEST_SUITE(CelonisModeAggTest, types_to_test);

/* update tests */
TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_empty_input_test) {
    // GIVEN
    const nullable_vec_t<TypeParam> input_column_data{};
    const nullable_val_t<TypeParam> expected_result{std::nullopt};
    // WHEN
    const auto test_ctx{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), empty_result{});
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_null_only_test) {
    // GIVEN
    const nullable_vec_t<TypeParam> input_column_data = {std::nullopt, std::nullopt};
    const nullable_val_t<TypeParam> expected_result{std::nullopt};
    // WHEN
    const auto test_ctx{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_clear_most_frequent_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", "3", "2", "3", "1"};
    const auto input_column_data{transform_to<TypeParam>(data1)}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    // WHEN
    const auto test_ctx{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_nulls_and_clear_most_frequent_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", std::nullopt, "2", "3", "1"};
    const auto input_column_data{
            transform_to<TypeParam>(data1)}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    // WHEN
    const auto test_ctx{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_multiple_most_frequent_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", "1", "2", "3", "1"};
    const auto input_column_data{
            transform_to<TypeParam>(data1)}; // 1 and 2 are most frequent
    const auto expected_result{transform_to<TypeParam>("1")};
    // WHEN
    const auto test_ctx{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

/* serialize_to_column (and deserialize without merge) tests */

TYPED_TEST(CelonisModeAggTest, celonis_mode_serialize_and_deserialize_with_nulls_and_clear_most_frequent_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", std::nullopt, "2", "3", "1"};
    const auto input_column_data{
            transform_to<TypeParam>(data1)}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    const auto test_ctx_and_serialized_data = this->run_update_and_serialize(input_column_data);
    // WHEN
    const auto test_ctx{this->run_deserialize(test_ctx_and_serialized_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_serialize_and_deserialize_with_multiple_most_frequent_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", "1", "2", "3", "1"};
    const auto input_column_data{
            transform_to<TypeParam>(data1)}; // 1 and 2 are most frequent
    const auto expected_result{transform_to<TypeParam>("1")};
    const auto test_ctx_and_serialized_data = this->run_update_and_serialize(input_column_data);
    // WHEN
    const auto test_ctx{this->run_deserialize(test_ctx_and_serialized_data)};
    // THEN
    ASSERT_EQ(expected_result, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

/* merge tests */

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_and_multiple_most_frequent_after_merge_test) {
    // GIVEN
    DatumProxyArray data1 = {"2", "1", "2", std::nullopt, "2", "3", "1"};
    const auto input_column_data{
            transform_to<TypeParam>(data1)}; // 2 is most frequent
    auto test_ctx{this->run_update(input_column_data)};
    const auto expected_result_before_merge{transform_to<TypeParam>("2")};
    DatumProxyArray merge_data1 = {std::nullopt, "1"};
    const auto serialized_data_1 = this->run_update_and_serialize(transform_to<TypeParam>(merge_data1));
    DatumProxyArray merge_data2 = {std::nullopt, "3"};
    const auto serialized_data_2 = this->run_update_and_serialize(transform_to<TypeParam>(merge_data2));
    const auto expected_result_after_merge{transform_to<TypeParam>("1")}; // after the merge, 1 and 2 are most frequent
    ASSERT_EQ(expected_result_before_merge, test_ctx.typed_state().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data_1, test_ctx);
    this->run_merge(serialized_data_2, test_ctx);
    // THEN
    ASSERT_EQ(expected_result_after_merge, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result_after_merge);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_test) {
    // GIVEN
    DatumProxyArray null_data = {std::nullopt, std::nullopt};
    const auto input_data_all_null{transform_to<TypeParam>(null_data)};
    auto test_ctx{this->run_update(input_data_all_null)};
    const auto serialized_data_1 = this->run_update_and_serialize(input_data_all_null);
    DatumProxyArray data3 = {"2", "3", "2"};
    const auto serialized_data_2 = this->run_update_and_serialize(transform_to<TypeParam>(data3));
    const auto expected_result_after_merge{transform_to<TypeParam>("2")}; // after the merge, 1 and 2 are most frequent
    ASSERT_EQ(std::nullopt, test_ctx.typed_state().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data_1, test_ctx);
    this->run_merge(serialized_data_2, test_ctx);
    // THEN
    ASSERT_EQ(expected_result_after_merge, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result_after_merge);
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_only_test) {
    // GIVEN
    DatumProxyArray null_data = {std::nullopt, std::nullopt};
    const auto input_data_all_null{transform_to<TypeParam>(null_data)};
    const nullable_val_t<TypeParam> expected_result{std::nullopt};
    auto test_ctx{this->run_update(input_data_all_null)};
    const auto serialized_data = this->run_update_and_serialize(input_data_all_null);
    ASSERT_EQ(std::nullopt, test_ctx.typed_state().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data, test_ctx);
    // THEN
    ASSERT_EQ(std::nullopt, test_ctx.typed_state().most_frequent_or_null());
    verify_final_column_result(this->run_finalize_to_column(test_ctx), expected_result);
}

/* test utils implementation */
namespace {

template <typename T>
[[nodiscard]] constexpr bool IS_BIGINT() {
    return std::is_same_v<T, RunTimeCppType<TYPE_BIGINT>>;
}

template <typename T>
[[nodiscard]] constexpr bool IS_DOUBLE() {
    return std::is_same_v<T, RunTimeCppType<TYPE_DOUBLE>>;
}

template <typename T>
[[nodiscard]] constexpr bool IS_VARCHAR() {
    return std::is_same_v<T, RunTimeCppType<TYPE_VARCHAR>>;
}

template <typename T>
[[nodiscard]] constexpr bool IS_DATETIME() {
    return std::is_same_v<T, RunTimeCppType<TYPE_DATETIME>>;
}

template <typename>
inline constexpr bool always_false_v{false};

template <typename T>
constexpr LogicalType AS_LOGICAL_TYPE() {
    if constexpr (IS_BIGINT<T>()) {
        return TYPE_BIGINT;
    } else if constexpr (IS_DOUBLE<T>()) {
        return TYPE_DOUBLE;
    } else if constexpr (IS_VARCHAR<T>()) {
        return TYPE_VARCHAR;
    } else if constexpr (IS_DATETIME<T>()) {
        return TYPE_DATETIME;
    } else {
        static_assert(always_false_v<T>);
    }
}

template <typename T>
std::optional<const T> transform_to(const DatumProxy& datum_proxy) {
    if (!datum_proxy.has_value()) {
        return std::nullopt;
    }

    const char* const datum_value_as_cstr{*datum_proxy};
    if constexpr (IS_BIGINT<T>()) {
        return {std::strtol(datum_value_as_cstr, nullptr, 10)};
    } else if constexpr (IS_DOUBLE<T>()) {
        return {std::strtod(datum_value_as_cstr, nullptr)};
    } else if constexpr (IS_VARCHAR<T>()) {
        return {Slice{datum_value_as_cstr}};
    } else if constexpr (IS_DATETIME<T>()) {
        return {TimestampValue::MIN_TIMESTAMP_VALUE.add<SECOND>(std::strtol(datum_value_as_cstr, nullptr, 10))};
    } else {
        static_assert(always_false_v<T>);
    }
}

template <typename T>
nullable_vec_t<T> transform_to(const DatumProxyArray& input_proxy_data) {
    nullable_vec_t<T> input_data{};
    for (const DatumProxy& datum_proxy : input_proxy_data) {
        input_data.push_back(transform_to<T>(datum_proxy));
    }
    return input_data;
}

void verify_final_column_result(const ColumnPtr& finalized_result_column, empty_result expected_result) {
    ASSERT_EQ(0, finalized_result_column->size());
}

template <typename T>
void verify_final_column_result(const ColumnPtr& finalized_result_column, nullable_val_t<T> expected_result) {
    ASSERT_EQ(1, finalized_result_column->size());
    const auto actual_datum_value{finalized_result_column->get(0)};
    ASSERT_TRUE(expected_result.has_value() ? actual_datum_value.get<T>() == *expected_result
                                            : actual_datum_value.is_null());
}

} // anonymous namespace

} // namespace starrocks
