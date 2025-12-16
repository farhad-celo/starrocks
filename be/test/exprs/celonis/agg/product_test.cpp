#include "exprs/celonis/agg/product.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"

namespace starrocks {

struct BigIntLTWrapper {
    static constexpr LogicalType GET_LOGICAL_TYPE() { return TYPE_BIGINT; }
};

struct DoubleLTWrapper {
    static constexpr LogicalType GET_LOGICAL_TYPE() { return TYPE_DOUBLE; }
};

using integer_t = RunTimeTypeTraits<BigIntLTWrapper::GET_LOGICAL_TYPE()>::CppType;
using floating_point_t = RunTimeTypeTraits<DoubleLTWrapper::GET_LOGICAL_TYPE()>::CppType;

template <typename T>
ProductAggregateState<T> make_initialized_state(T v) {
    return ProductAggregateState<T>{ProductAggregateState<T>::AggregationStage::INITIALIZED, v};
}

template <typename T>
ProductAggregateState<T> make_uninitialized_state() {
    return ProductAggregateState<T>{};
}

template <typename T>
ProductAggregateState<T> make_overflowed_state() {
    return ProductAggregateState<T>{ProductAggregateState<T>::AggregationStage::OVERFLOW, T{}};
}

template <typename T>
void test_product_regular_state_update(T initial_value, T next_value, T expected_value) {
    ProductAggregateState<T> initial_state = make_initialized_state(initial_value);
    initial_state.update(next_value);
    ASSERT_TRUE(initial_state.is_initialized());
    ASSERT_EQ(expected_value, initial_state.get_product());
}

TEST(CelonisProductStateUpdate, product_state_update_integer_simple) {
    test_product_regular_state_update<integer_t>(0, 1, 0);
    test_product_regular_state_update<integer_t>(5, 0, 0);
    test_product_regular_state_update<integer_t>(3, 2, 6);
    test_product_regular_state_update<integer_t>(1, 700, 700);
    test_product_regular_state_update<integer_t>(24, 12, 288);
    test_product_regular_state_update<integer_t>(10, -3, -30);
    test_product_regular_state_update<integer_t>(-3, -3, 9);
}

TEST(CelonisProductStateUpdate, product_state_update_floating_point_simple) {
    test_product_regular_state_update<floating_point_t>(0, 1.0, 0);
    test_product_regular_state_update<floating_point_t>(5, M_PI, 5 * M_PI);
    test_product_regular_state_update<floating_point_t>(0, M_PI, 0);
    test_product_regular_state_update<floating_point_t>(M_PI, M_PI, std::pow(M_PI, 2));
    test_product_regular_state_update<floating_point_t>(M_PI, -1.5, -1.5 * M_PI);
}

bool does_overflow(integer_t initial_value, integer_t next_value) {
    auto state = make_initialized_state(initial_value);
    state.update(next_value);
    return state.has_overflowed();
}

TEST(CelonisProductStateUpdate, product_state_update_integer_overflow) {
    ASSERT_TRUE(does_overflow(std::numeric_limits<integer_t>::max() / 2 + 1, 2));
    ASSERT_FALSE(does_overflow(std::numeric_limits<integer_t>::max() / 2 - 1, 2));
}

TEST(CelonisProductStateUpdate, product_state_update_integer_remains_overflowed) {
    auto state = make_overflowed_state<integer_t>();
    state.update(5);
    ASSERT_TRUE(state.has_overflowed());
}

TEST(CelonisProductStateUpdate, product_state_update_unitialized_becomes_initialized) {
    auto state = make_uninitialized_state<integer_t>();
    state.update(5);
    ASSERT_TRUE(state.is_initialized());
}

TEST(CelonisProductStateMerge, product_state_merge_initialized_with_itialized) {
    auto state1 = make_initialized_state<integer_t>(3);
    auto state2 = make_initialized_state<integer_t>(4);
    state1.merge(state2);
    ASSERT_TRUE(state1.is_initialized());
    ASSERT_EQ(12, state1.get_product());
}

TEST(CelonisProductStateMerge, product_state_merge_unitialized_with_initialized) {
    auto state1 = make_uninitialized_state<integer_t>();
    auto state2 = make_initialized_state<integer_t>(4);
    state1.merge(state2);
    ASSERT_TRUE(state1.is_initialized());
    ASSERT_EQ(4, state1.get_product());
}

TEST(CelonisProductStateMerge, product_state_merge_initialized_with_uninitialized) {
    auto state1 = make_initialized_state<integer_t>(3);
    auto state2 = make_uninitialized_state<integer_t>();
    state1.merge(state2);
    ASSERT_TRUE(state1.is_initialized());
    ASSERT_EQ(3, state1.get_product());
}

TEST(CelonisProductStateMerge, product_state_merge_overflowed_with_initialized) {
    auto state1 = make_overflowed_state<integer_t>();
    auto state2 = make_initialized_state<integer_t>(4);
    state1.merge(state2);
    ASSERT_TRUE(state1.has_overflowed());
}

TEST(CelonisProductStateMerge, product_state_merge_initialized_with_overflowed) {
    auto state1 = make_initialized_state<integer_t>(3);
    auto state2 = make_overflowed_state<integer_t>();
    state1.merge(state2);
    ASSERT_TRUE(state1.has_overflowed());
}

TEST(CelonisProductStateMerge, product_state_merge_both_overflowed) {
    auto state1 = make_overflowed_state<integer_t>();
    auto state2 = make_overflowed_state<integer_t>();
    state1.merge(state2);
    ASSERT_TRUE(state1.has_overflowed());
}

TEST(CelonisProductStateMerge, product_state_merge_both_unitialized) {
    auto state1 = make_uninitialized_state<integer_t>();
    auto state2 = make_uninitialized_state<integer_t>();
    state1.merge(state2);
    ASSERT_FALSE(state1.is_initialized());
}

TEST(CelonisProductStateMerge, product_state_merge_unitialized_with_overflowed) {
    auto state1 = make_uninitialized_state<integer_t>();
    auto state2 = make_overflowed_state<integer_t>();
    state1.merge(state2);
    ASSERT_TRUE(state1.has_overflowed());
}

TEST(CelonisProductStateMerge, product_state_merge_overflowed_with_uninitialized) {
    auto state1 = make_overflowed_state<integer_t>();
    auto state2 = make_uninitialized_state<integer_t>();
    state1.merge(state2);
    ASSERT_TRUE(state1.has_overflowed());
}

template <typename T>
NullableColumn::Ptr create_serialization_column() {
    auto stage_field_data_col = ProductAggregateState<T>::StageFieldcolumnType::create();
    auto product_field_data_col = ProductAggregateState<T>::ProductFieldColumnType::create();
    auto stage_field_col = NullableColumn::create(std::move(stage_field_data_col), NullColumn::create());
    auto product_field_col = NullableColumn::create(std::move(product_field_data_col), NullColumn::create());
    Columns columns;
    columns.push_back(std::move(stage_field_col));
    columns.push_back(std::move(product_field_col));
    std::vector<std::string> field_names = {"Stage", "Product"};
    auto struct_col = StructColumn::create(std::move(columns), std::move(field_names));
    return NullableColumn::create(std::move(struct_col), NullColumn::create());
}

template <typename T>
void verify_serialization_deserialization(const ProductAggregateState<T>& state) {
    static_assert(std::is_same_v<T, integer_t> || std::is_same_v<T, floating_point_t>);
    NullableColumn::Ptr serialization_column = create_serialization_column<T>();
    auto* struct_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(serialization_column.get()));
    state.append_to_struct_column(*struct_column);
    auto deserialized_state = ProductAggregateState<T>::read_from_struct_column(*struct_column, 0);
    ASSERT_EQ(state.get_stage(), deserialized_state.get_stage());
    if (state.is_initialized()) {
        ASSERT_EQ(state.get_product(), deserialized_state.get_product());
    }
}

TEST(CelonisProductStateSerializeDeserialize, product_state_serialize_deserialize_integer) {
    verify_serialization_deserialization(make_initialized_state<integer_t>(0));
    verify_serialization_deserialization(make_initialized_state<integer_t>(43));
    verify_serialization_deserialization(make_initialized_state<integer_t>(-324));
    verify_serialization_deserialization(make_uninitialized_state<integer_t>());
    verify_serialization_deserialization(make_overflowed_state<integer_t>());
}

TEST(CelonisProductStateSerializeDeserialize, product_state_serialize_deserialize_floating_point) {
    verify_serialization_deserialization(make_initialized_state<floating_point_t>(0));
    verify_serialization_deserialization(make_initialized_state<floating_point_t>(0.000000001));
    verify_serialization_deserialization(make_initialized_state<floating_point_t>(M_PI * 10000));
    verify_serialization_deserialization(make_uninitialized_state<floating_point_t>());
    verify_serialization_deserialization(make_overflowed_state<floating_point_t>());
}

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

template <typename T>
NullableColumn::Ptr create_nullable_column_from_data(std::vector<std::optional<T>> values) {
    auto column = NullableColumn::create(FixedLengthColumn<T>::create(), NullColumn::create());
    std::for_each(values.begin(), values.end(), [&column](const std::optional<T>& val) {
        if (val.has_value()) {
            column->append_datum(val.value());
        } else {
            column->append_nulls(1);
        }
    });

    return column;
}

template <typename LT_WRAPPER>
class CelonisProductTest : public testing::TestWithParam<LogicalType> {
public:
    static constexpr LogicalType LOGICAL_TYPE{LT_WRAPPER::GET_LOGICAL_TYPE()};
    using RunTimeCppType = typename RunTimeTypeTraits<LOGICAL_TYPE>::CppType;

    CelonisProductTest() = default;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();

        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LOGICAL_TYPE))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LOGICAL_TYPE));
        local_ctx = std::unique_ptr<FunctionContext>{
                FunctionContext::create_test_context(std::move(arg_types), return_type)};

        aggregate_func = get_aggregate_function("celonis_product", LOGICAL_TYPE, LOGICAL_TYPE, false);
    }
    void TearDown() override { delete utils; }

    void add_column(std::vector<std::optional<RunTimeCppType>> values) {
        auto managed_state = ManagedAggrState::create(this->ctx, this->aggregate_func);
        auto column = create_nullable_column_from_data(std::move(values));

        const Column* col_ptr = column.get();
        aggregate_func->update_batch_single_state(this->local_ctx.get(), column->size(), &col_ptr,
                                                  managed_state->state());
        managed_states.emplace_back(std::move(managed_state));
    }

    std::optional<RunTimeCppType> compute_product() {
        NullableColumn::Ptr serialization_column = create_serialization_column<RunTimeCppType>();
        auto result_column = NullableColumn::create(RunTimeColumnType<LOGICAL_TYPE>::create(), NullColumn::create());
        if (managed_states.empty()) {
            return std::nullopt;
        }
        for (size_t i = 1; i < managed_states.size(); i++) {
            aggregate_func->serialize_to_column(local_ctx.get(), managed_states[i]->state(),
                                                serialization_column.get());
            serialization_column->check_or_die();
        }

        for (size_t i = 0; i < managed_states.size() - 1; i++) {
            aggregate_func->merge(local_ctx.get(), serialization_column.get(), managed_states.front()->state(), i);
        }

        aggregate_func->finalize_to_column(local_ctx.get(), managed_states.front()->state(), result_column.get());
        result_column->check_or_die();

        if (result_column->is_null(0)) {
            return std::nullopt;
        }
        return result_column->get(0).template get<RunTimeCppType>();
    }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
    std::unique_ptr<FunctionContext> local_ctx{};
    const AggregateFunction* aggregate_func{};
    std::vector<std::unique_ptr<ManagedAggrState>> managed_states{};
};

using TestedTypes = ::testing::Types<BigIntLTWrapper, DoubleLTWrapper>;

TYPED_TEST_SUITE(CelonisProductTest, TestedTypes);

TYPED_TEST(CelonisProductTest, product_test_simple_one_column) {
    this->add_column({1, 2, 3});

    auto result = this->compute_product();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(6, result.value());
}

TYPED_TEST(CelonisProductTest, product_test_simple_two_columns) {
    this->add_column({1, 2, 3});
    this->add_column({4, 5, 6});

    auto result = this->compute_product();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(720, result.value());
}

TYPED_TEST(CelonisProductTest, product_test_simple_three_columns) {
    this->add_column({1, 2, 3});
    this->add_column({4, 5, 6});
    this->add_column({7, 8, 9});

    auto result = this->compute_product();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(362880, result.value());
}

TYPED_TEST(CelonisProductTest, product_test_null_values) {
    this->add_column({1, 2, {}});
    this->add_column({4, {}, 6});

    auto result = this->compute_product();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(48, result.value());
}

TYPED_TEST(CelonisProductTest, product_test_one_column_all_null) {
    this->add_column({1, 2, {}});
    this->add_column({{}, {}, {}});

    auto result = this->compute_product();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(2, result.value());
}

TYPED_TEST(CelonisProductTest, product_test_all_null) {
    this->add_column({{}, {}, {}});
    this->add_column({{}, {}, {}});

    auto result = this->compute_product();

    ASSERT_FALSE(result.has_value());
}

TYPED_TEST(CelonisProductTest, product_test_overflow) {
    this->add_column({1, 2, 3});
    this->add_column({4, 5, std::numeric_limits<integer_t>::max()});
    auto result = this->compute_product();

    if constexpr (std::is_integral_v<typename decltype(result)::value_type>) {
        ASSERT_FALSE(result.has_value());
    } else {
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(120.0 * static_cast<double>(std::numeric_limits<integer_t>::max()), result.value());
    }
}

TYPED_TEST(CelonisProductTest, product_test_convert_to_serialize_format) {
    using T = typename decltype(this->compute_product())::value_type;
    using RawStageFieldType = typename ProductAggregateState<T>::StageFieldcolumnType::ValueType;
    auto input_column = create_nullable_column_from_data<T>({2, {}, 4, {}});
    NullableColumn::Ptr serialization_column = create_serialization_column<T>();
    ColumnPtr serialization_column_abstract = serialization_column;

    size_t chunk_size = 4;

    this->aggregate_func->convert_to_serialize_format(this->local_ctx.get(), {input_column}, 4,
                                                      &serialization_column_abstract);

    serialization_column->check_or_die();

    auto* serialized_struct_column =
            down_cast<StructColumn*>(ColumnHelper::get_data_column(serialization_column.get()));
    auto* serialized_stage_column = down_cast<FixedLengthColumn<RawStageFieldType>*>(
            ColumnHelper::get_data_column(serialized_struct_column->fields_column()[0].get()));
    auto* serialized_product_column = down_cast<FixedLengthColumn<T>*>(
            ColumnHelper::get_data_column(serialized_struct_column->fields_column()[1].get()));
    auto& serialized_stage_data = serialized_stage_column->get_data();
    auto& serialized_product_data = serialized_product_column->get_data();

    ASSERT_EQ(serialization_column->size(), chunk_size);
    ASSERT_EQ(serialized_stage_data[0],
              static_cast<RawStageFieldType>(ProductAggregateState<T>::AggregationStage::INITIALIZED));
    ASSERT_EQ(serialized_stage_data[1],
              static_cast<RawStageFieldType>(ProductAggregateState<T>::AggregationStage::UNINITIALIZED));
    ASSERT_EQ(serialized_stage_data[2],
              static_cast<RawStageFieldType>(ProductAggregateState<T>::AggregationStage::INITIALIZED));
    ASSERT_EQ(serialized_stage_data[3],
              static_cast<RawStageFieldType>(ProductAggregateState<T>::AggregationStage::UNINITIALIZED));
    ASSERT_EQ(serialized_product_data[0], 2);
    ASSERT_EQ(serialized_product_data[2], 4);
}

TYPED_TEST(CelonisProductTest, product_test_convert_to_serialize_format_and_merge) {
    using T = typename decltype(this->compute_product())::value_type;
    auto input_column = create_nullable_column_from_data<T>({2, {}, 4, {}});
    auto result_column = NullableColumn::create(FixedLengthColumn<T>::create(), NullColumn::create());
    NullableColumn::Ptr serialization_column = create_serialization_column<T>();
    ColumnPtr serialization_column_abstract = serialization_column;

    size_t chunk_size = 4;

    auto managed_state = ManagedAggrState::create(this->ctx, this->aggregate_func);

    this->aggregate_func->convert_to_serialize_format(this->local_ctx.get(), {input_column}, chunk_size,
                                                      &serialization_column_abstract);

    for (size_t i = 0; i < chunk_size; i++) {
        this->aggregate_func->merge(this->local_ctx.get(), serialization_column.get(), managed_state->state(), i);
    }

    this->aggregate_func->finalize_to_column(this->local_ctx.get(), managed_state->state(), result_column.get());

    auto result_data = ColumnHelper::get_data_column(result_column.get());

    ASSERT_EQ(result_column->size(), 1);
    ASSERT_EQ(down_cast<FixedLengthColumn<T>*>(result_data)->get_data()[0], 8);
    ASSERT_FALSE(result_column->is_null(0));
}

} // namespace starrocks