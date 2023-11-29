#include "exprs/celonis/mode_agg.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "column/column_builder.h"
#include "exprs/agg/aggregate_factory.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"
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

} // namespace

/* Types to test for: BIGINT, DOUBLE, VARCHAR, DATETIME */
using types_to_test = testing::Types<std::int64_t, double, Slice, TimestampValue>;

template <typename T>
class CelonisModeAggTest : public testing::Test {
public:
    CelonisModeAggTest() = default;
    using ValueType = T;
    static constexpr auto LT{AS_LOGICAL_TYPE<ValueType>()};
    using StateType = CelonisModeState<LT>;

    void SetUp() override {
        utils_ = new FunctionUtils();
        ctx_ = utils_->get_fn_ctx();
        func_ = get_aggregate_function("celonis_mode", LT, LT, false);
    }

    void TearDown() override { delete utils_; }

    [[nodiscard]] std::unique_ptr<ManagedAggrState> make_new_state() const {
        return ManagedAggrState::create(ctx_, func_);
    }

    struct managed_state_wrapper {
        managed_state_wrapper(std::unique_ptr<ManagedAggrState>&& managed_state)
                : managed_state_ptr{std::move(managed_state)} {}
        [[nodiscard]] const StateType& typed() const {
            return reinterpret_cast<const StateType&>(*managed_state_ptr->state());
        }
        operator const ManagedAggrState&() const { return *managed_state_ptr; }
        std::unique_ptr<ManagedAggrState> managed_state_ptr;
    };
    [[nodiscard]] managed_state_wrapper run_update(const nullable_vec_t<ValueType>& input_column_data) {
        auto managed_state{this->make_new_state()};
        const auto input_column{build_column(input_column_data)};
        std::vector<const Column*> raw_input_column_ptrs = {input_column.get()};
        // execute aggregate function: update
        func_->update_batch_single_state(ctx_, input_column_data.size(), raw_input_column_ptrs.data(),
                                         managed_state->state());
        return {std::move(managed_state)};
    }

    [[nodiscard]] std::shared_ptr<BinaryColumn> run_serialize(const ManagedAggrState& managed_state) {
        auto serialization_dst{BinaryColumn::create()};
        // execute aggregate function: serialize
        func_->serialize_to_column(ctx_, managed_state.state(), serialization_dst.get());
        return serialization_dst;
    }

    /** Does not take a state as this function only updates a temporary state which is returned serialized */
    [[nodiscard]] std::shared_ptr<BinaryColumn> run_update_and_serialize(
            const nullable_vec_t<ValueType>& input_column_data) {
        const auto managed_state_wrapper{run_update(input_column_data)};
        return run_serialize(managed_state_wrapper);
    }

    /** Returns the deserialized state of the given column (by deserializing and merging with an empty state) */
    [[nodiscard]] managed_state_wrapper run_deserialize(const std::shared_ptr<BinaryColumn>& deserialization_src) {
        auto managed_state{this->make_new_state()};
        func_->merge(ctx_, deserialization_src.get(), managed_state->state(), 0);
        return {std::move(managed_state)};
    }

    void run_merge(const std::shared_ptr<BinaryColumn>& deserialization_src, const ManagedAggrState& managed_state) {
        func_->merge(ctx_, deserialization_src.get(), managed_state.state(), 0);
    }

private:
    [[nodiscard]] static ColumnPtr build_column(const nullable_vec_t<ValueType>& input_column_data) {
        ColumnBuilder<LT> builder{config::vector_chunk_size};
        for (const auto& input_column_value : input_column_data) {
            if (input_column_value.has_value()) {
                builder.append(*input_column_value);
            } else {
                builder.append_null();
            }
        }
        return builder.build_nullable_column();
    }
    FunctionUtils* utils_{};
    FunctionContext* ctx_{};
    const AggregateFunction* func_;
};

TYPED_TEST_SUITE(CelonisModeAggTest, types_to_test);

/* update tests */
TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_empty_input_test) {
    // GIVEN
    const nullable_vec_t<TypeParam> input_column_data{};
    const nullable_val_t<TypeParam> expected_result{std::nullopt};
    // WHEN
    const auto state_after_update{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, state_after_update.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_null_only_test) {
    // GIVEN
    const nullable_vec_t<TypeParam> input_column_data = {std::nullopt, std::nullopt};
    const nullable_val_t<TypeParam> expected_result{std::nullopt};
    // WHEN
    const auto state_after_update{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, state_after_update.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_clear_most_frequent_test) {
    // GIVEN
    const auto input_column_data{transform_to<TypeParam>({"2", "1", "2", "3", "2", "3", "1"})}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    // WHEN
    const auto state_after_update{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, state_after_update.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_nulls_and_clear_most_frequent_test) {
    // GIVEN
    const auto input_column_data{
            transform_to<TypeParam>({"2", "1", "2", std::nullopt, "2", "3", "1"})}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    // WHEN
    const auto state_after_update{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, state_after_update.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_update_with_multiple_most_frequent_test) {
    // GIVEN
    const auto input_column_data{
            transform_to<TypeParam>({"2", "1", "2", "1", "2", "3", "1"})}; // 1 and 2 are most frequent
    const auto expected_result{transform_to<TypeParam>("1")};
    // WHEN
    const auto state_after_update{this->run_update(input_column_data)};
    // THEN
    ASSERT_EQ(expected_result, state_after_update.typed().most_frequent_or_null());
}

/* serialize_to_column (and deserialize without merge) tests */

TYPED_TEST(CelonisModeAggTest, celonis_mode_serialize_and_deserialize_with_nulls_and_clear_most_frequent_test) {
    // GIVEN
    const auto input_column_data{
            transform_to<TypeParam>({"2", "1", "2", std::nullopt, "2", "3", "1"})}; // 2 is most frequent
    const auto expected_result{transform_to<TypeParam>("2")};
    const auto serialized_data{this->run_update_and_serialize(input_column_data)};
    // WHEN
    const auto managed_state_after_deserialization{this->run_deserialize(serialized_data)};
    // THEN
    ASSERT_EQ(expected_result, managed_state_after_deserialization.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_serialize_and_deserialize_with_multiple_most_frequent_test) {
    // GIVEN
    const auto input_column_data{
            transform_to<TypeParam>({"2", "1", "2", "1", "2", "3", "1"})}; // 1 and 2 are most frequent
    const auto expected_result{transform_to<TypeParam>("1")};
    const auto serialized_data{this->run_update_and_serialize(input_column_data)};
    // WHEN
    const auto managed_state_after_deserialization{this->run_deserialize(serialized_data)};
    // THEN
    ASSERT_EQ(expected_result, managed_state_after_deserialization.typed().most_frequent_or_null());
}

/* merge tests */

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_and_multiple_most_frequent_after_merge_test) {
    // GIVEN
    const auto input_column_data{
            transform_to<TypeParam>({"2", "1", "2", std::nullopt, "2", "3", "1"})}; // 2 is most frequent
    const auto state{this->run_update(input_column_data)};
    const auto expected_result_before_merge{transform_to<TypeParam>("2")};
    const auto serialized_data_1{this->run_update_and_serialize(transform_to<TypeParam>({std::nullopt, "1"}))};
    const auto serialized_data_2{this->run_update_and_serialize(transform_to<TypeParam>({std::nullopt, "3"}))};
    const auto expected_result_after_merge{transform_to<TypeParam>("1")}; // after the merge, 1 and 2 are most frequent
    ASSERT_EQ(expected_result_before_merge, state.typed().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data_1, state);
    this->run_merge(serialized_data_2, state);
    // THEN
    ASSERT_EQ(expected_result_after_merge, state.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_test) {
    // GIVEN
    const auto input_data_all_null{transform_to<TypeParam>({std::nullopt, std::nullopt})};
    const auto state{this->run_update(input_data_all_null)};
    const auto serialized_data_1{this->run_update_and_serialize(input_data_all_null)};
    const auto serialized_data_2{this->run_update_and_serialize(transform_to<TypeParam>({"2", "3", "2"}))};
    const auto expected_result_after_merge{transform_to<TypeParam>("2")}; // after the merge, 1 and 2 are most frequent
    ASSERT_EQ(std::nullopt, state.typed().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data_1, state);
    this->run_merge(serialized_data_2, state);
    // THEN
    ASSERT_EQ(expected_result_after_merge, state.typed().most_frequent_or_null());
}

TYPED_TEST(CelonisModeAggTest, celonis_mode_merge_with_nulls_only_test) {
    // GIVEN
    const auto input_data_all_null{transform_to<TypeParam>({std::nullopt, std::nullopt})};
    const auto state{this->run_update(input_data_all_null)};
    const auto serialized_data{this->run_update_and_serialize(input_data_all_null)};
    ASSERT_EQ(std::nullopt, state.typed().most_frequent_or_null());
    // WHEN
    this->run_merge(serialized_data, state);
    // THEN
    ASSERT_EQ(std::nullopt, state.typed().most_frequent_or_null());
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

} // anonymous namespace

} // namespace starrocks
