#include "exprs/celonis/agg/abc_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "column/struct_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

namespace {

static double ABS_ERROR = 1e-6;

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

} // namespace

class CelonisBuildAbcModelTest : public testing::Test {
protected:
    CelonisBuildAbcModelTest() = default;

    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        TypeDescriptor type_return_varchar;
        type_return_varchar.type = LogicalType::TYPE_VARCHAR;
        return type_return_varchar;
    }

    std::unique_ptr<FunctionContext> get_ctx(LogicalType logical_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(logical_type), // input
                TypeDescriptor::from_logical_type(TYPE_BIGINT),  // pk_hash
                TypeDescriptor::from_logical_type(TYPE_DOUBLE),  // sample_ratio
                TypeDescriptor::from_logical_type(TYPE_DOUBLE),  // A
                TypeDescriptor::from_logical_type(TYPE_DOUBLE),  // B
        };
        auto return_type = TypeDescriptor::from_logical_type(TYPE_VARCHAR);
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        runtime_states_.emplace_back(std::make_unique<RuntimeState>());
        return std::unique_ptr<FunctionContext>(FunctionContext::create_context(
                runtime_states_.back().get(), mem_pools_.back().get(), return_type, std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*> RunUpdate(
            LogicalType logical_type, const DatumArray& input, const DatumArray& pk_hash, double sample_ratio,
            double ratio_a, double ratio_b) {
        auto local_ctx = get_ctx(logical_type);

        const AggregateFunction* func =
                get_aggregate_function("celonis_build_abc_model", logical_type, TYPE_VARCHAR, false);

        auto input_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(logical_type), true);
        for (const auto& datum : input) {
            input_col->append_datum(datum);
        }
        auto pk_hash_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), true);
        for (const auto& datum : pk_hash) {
            pk_hash_col->append_datum(datum);
        }
        auto sample_ratio_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(sample_ratio, input.size());
        auto ratio_a_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(ratio_a, input.size());
        auto ratio_b_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(ratio_b, input.size());

        std::vector<const Column*> raw_columns;
        raw_columns.resize(5);
        raw_columns[0] = input_col.get();
        raw_columns[1] = pk_hash_col.get();
        raw_columns[2] = sample_ratio_col.get();
        raw_columns[3] = ratio_a_col.get();
        raw_columns[4] = ratio_b_col.get();
        local_ctx->set_constant_columns({nullptr, nullptr, sample_ratio_col, ratio_a_col, ratio_b_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    bool create_model(const std::string& model, std::vector<std::pair<double, double>>& ranges,
                      std::map<double, std::vector<double>>& num_to_probs) {
        std::vector<std::string> parts;
        boost::split(parts, model, boost::is_any_of(":"));
        if (parts.size() != 2) {
            return false;
        }
        std::vector<std::string> boundary_strs;
        boost::split(boundary_strs, parts[0], boost::is_any_of(","));
        if (boundary_strs.size() != 6) {
            return false;
        }
        std::vector<double> boundaries;
        for (const auto& boundary_str : boundary_strs) {
            try {
                auto boundary = boost::lexical_cast<double>(boundary_str);
                boundaries.push_back(boundary);
            } catch (const boost::bad_lexical_cast& e) {
                return false;
            }
        }
        ranges.resize(4);
        ranges[1] = std::make_pair(boundaries[0], boundaries[1]);
        ranges[2] = std::make_pair(boundaries[2], boundaries[3]);
        ranges[3] = std::make_pair(boundaries[4], boundaries[5]);
        if (!parts[1].empty()) {
            std::vector<std::string> num_section_strs;
            boost::split(num_section_strs, parts[1], boost::is_any_of(";"));
            for (const auto& num_section_str : num_section_strs) {
                std::vector<std::string> value_strs;
                boost::split(value_strs, num_section_str, boost::is_any_of(","));
                if (value_strs.size() != 4) {
                    return false;
                }
                std::vector<double> probs;
                double num;
                for (int i = 0; i < 4; ++i) {
                    try {
                        if (i == 0) {
                            num = boost::lexical_cast<double>(value_strs[0]);
                        } else {
                            auto value = boost::lexical_cast<double>(value_strs[i]);
                            probs.push_back(value);
                        }
                    } catch (const boost::bad_lexical_cast& e) {
                        return false;
                    }
                }
                double prob1 = probs[0];
                double prob2 = probs[1];
                double prob3 = probs[2];
                if (prob1 < -EPS || prob1 > 1.0 + EPS || prob2 < -EPS || prob2 > 1.0 + EPS || prob3 < -EPS ||
                    prob3 > 1.0 + EPS) {
                    return false;
                }
                num_to_probs[num] = {0.0, prob1, prob2, prob3};
            }
        }
        return true;
    }

    template <LogicalType LT>
    void Run(const DatumArray& input, const DatumArray& pk_hash, double sample_ratio, double ratio_a, double ratio_b,
             const std::vector<std::pair<double, double>>& expected_ranges,
             const std::map<double, std::vector<double>>& expected_num_to_probs, bool is_null = false) {
        auto [local_ctx, state, func] = RunUpdate(LT, input, pk_hash, sample_ratio, ratio_a, ratio_b);

        auto result = ColumnHelper::create_column(get_return_type(), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());
        ASSERT_EQ(1, result->size());
        if (is_null) {
            EXPECT_TRUE(result->get(0).is_null());
        } else {
            const std::string model = result->get(0).get_slice().to_string();
            std::vector<std::pair<double, double>> ranges;
            std::map<double, std::vector<double>> num_to_probs;
            ASSERT_TRUE(create_model(model, ranges, num_to_probs));
            EXPECT_EQ(4, ranges.size());
            for (int i = 1; i <= 3; ++i) {
                EXPECT_EQ(expected_ranges[i].first, ranges[i].first);
                EXPECT_EQ(expected_ranges[i].second, ranges[i].second);
            }
            for (const auto& [num, expected_probs] : expected_num_to_probs) {
                auto it = num_to_probs.find(num);
                ASSERT_TRUE(it != num_to_probs.end());
                const auto probs = it->second;
                for (int i = 1; i <= 3; ++i) {
                    EXPECT_NEAR(expected_probs[i], probs[i], ABS_ERROR);
                }
            }
        }
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
    std::vector<std::unique_ptr<RuntimeState>> runtime_states_;
};

TEST_F(CelonisBuildAbcModelTest, cancellation_work) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{50L, 30L};
    auto input2 = DatumArray{8L, 7L, 5L};
    auto pk_hash1 = DatumArray{1L, 2L};
    auto pk_hash2 = DatumArray{3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    local_ctx1->state()->set_is_cancelled(true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    ASSERT_TRUE(local_ctx1->has_error());
}

TEST_F(CelonisBuildAbcModelTest, bigint_unique_values_merge) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{50L, 30L};
    auto input2 = DatumArray{8L, 7L, 5L};
    auto pk_hash1 = DatumArray{1L, 2L};
    auto pk_hash2 = DatumArray{3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    EXPECT_EQ("30,50,7,8,5,5:", result->get(0).get_slice());
}

TEST_F(CelonisBuildAbcModelTest, bigint_constant_values_merge) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{1L, 1L, 1L, 1L, 1L};
    auto input2 = DatumArray{1L, 1L, 1L, 1L, 1L};
    auto pk_hash1 = DatumArray{1L, 2L, 3L, 4L, 5L};
    auto pk_hash2 = DatumArray{6L, 7L, 8L, 9L, 10L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    EXPECT_EQ("1,1,1,1,1,1:1,0.800000,0.100000,0.100000", result->get(0).get_slice());
}

TEST_F(CelonisBuildAbcModelTest, double_unique_values_merge) {
    auto logical_type = TYPE_DOUBLE;
    auto input1 = DatumArray{50.5, 29.5};
    auto input2 = DatumArray{8.5, 6.5, 5.0};
    auto pk_hash1 = DatumArray{1L, 2L};
    auto pk_hash2 = DatumArray{3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    EXPECT_EQ("29.500000,50.500000,6.500000,8.500000,5.000000,5.000000:", result->get(0).get_slice());
}

TEST_F(CelonisBuildAbcModelTest, double_constant_values_merge) {
    auto logical_type = TYPE_DOUBLE;
    auto input1 = DatumArray{1.2, 1.2, 1.2, 1.2, 1.2};
    auto input2 = DatumArray{1.2, 1.2, 1.2, 1.2, 1.2};
    auto pk_hash1 = DatumArray{1L, 2L, 3L, 4L, 5L};
    auto pk_hash2 = DatumArray{6L, 7L, 8L, 9L, 10L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ("1.200000,1.200000,1.200000,1.200000,1.200000,1.200000:1.200000,0.800000,0.100000,0.100000",
              result->get(0).get_slice());
}

TEST_F(CelonisBuildAbcModelTest, merge_to_new_state) {
    auto logical_type = TYPE_DOUBLE;
    auto input1 = DatumArray{50.5, 29.5};
    auto input2 = DatumArray{8.5, 6.5, 5.0};
    auto pk_hash1 = DatumArray{1L, 2L};
    auto pk_hash2 = DatumArray{3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, pk_hash1, sample_ratio, ratio_a, ratio_b);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, pk_hash2, sample_ratio, ratio_a, ratio_b);

    // Serialize state1 and state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    auto local_ctx3 = get_ctx(logical_type);
    auto sample_ratio_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(sample_ratio, 2);
    auto ratio_a_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(ratio_a, 2);
    auto ratio_b_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(ratio_b, 2);
    local_ctx3->set_constant_columns({nullptr, nullptr, sample_ratio_col, ratio_a_col, ratio_b_col});
    auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
    // Merge state1 and state2 into new state3.
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);
    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

    EXPECT_EQ("29.500000,50.500000,6.500000,8.500000,5.000000,5.000000:", result->get(0).get_slice());
}

TEST_F(CelonisBuildAbcModelTest, bigint_single_value) {
    {
        auto input = DatumArray{50L};
        auto pk_hash = DatumArray{1L};
        double sample_ratio = 1.0;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {0, -1}, {0, -1}, {50, 50}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
    {
        auto input = DatumArray{50L};
        auto pk_hash = DatumArray{1L};
        double sample_ratio = 1.0;
        double ratio_a = 1.0;
        double ratio_b = 0.0;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {50, 50}, {0, -1}, {0, -1}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
}

TEST_F(CelonisBuildAbcModelTest, double_single_value) {
    {
        auto input = DatumArray{50.5};
        auto pk_hash = DatumArray{1L};
        double sample_ratio = 1.0;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {0, -1}, {0, -1}, {50.5, 50.5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_DOUBLE>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
    {
        auto input = DatumArray{50.5};
        auto pk_hash = DatumArray{1L};
        double sample_ratio = 1.0;
        double ratio_a = 1.0;
        double ratio_b = 0.0;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {50.5, 50.5}, {0, -1}, {0, -1}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_DOUBLE>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
}

TEST_F(CelonisBuildAbcModelTest, null_values) {
    {
        auto input = DatumArray{70L, 50L, 30L, 8L, kNullDatum, 7L, 5L, kNullDatum};
        auto pk_hash = DatumArray{kNullDatum, 1L, 2L, 3L, kNullDatum, 4L, 5L, 6L};
        double sample_ratio = 1.0;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {30, 50}, {7, 8}, {5, 5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
    {
        auto input = DatumArray{70.5, 50.5, 30.5, 8.5, kNullDatum, 7.5, 5.5, kNullDatum};
        auto pk_hash = DatumArray{kNullDatum, 1L, 2L, 3L, kNullDatum, 4L, 5L, 6L};
        double sample_ratio = 1.0;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {30.5, 50.5}, {7.5, 8.5}, {5.5, 5.5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_DOUBLE>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
}

TEST_F(CelonisBuildAbcModelTest, bigint_unique_values) {
    {
        auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
        auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
        double sample_ratio = 1.0;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {30, 50}, {7, 8}, {5, 5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
    {
        auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
        auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
        double sample_ratio = 1.0;
        double ratio_a = 0.5;
        double ratio_b = 0.3;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {50, 50}, {30, 30}, {5, 8}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
}

TEST_F(CelonisBuildAbcModelTest, bigint_unique_values_sample_works) {
    {
        auto input = DatumArray{70L, 50L, 30L, 8L, 7L, 5L, 3L};
        // both 70L and 3L should be dropped
        auto pk_hash = DatumArray{7378697629483820640L, 1L, 2L, 3L, 4L, 5L, -7378697629483820645L};
        double sample_ratio = 0.5;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {30, 50}, {7, 8}, {5, 5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
    {
        auto input = DatumArray{70.5, 50.5, 30.5, 8.5, 7.5, 5.5, 3.5};
        auto pk_hash = DatumArray{7378697629483820640L, 1L, 2L, 3L, 4L, 5L, 7378697629483820645L};
        double sample_ratio = 0.5;
        double ratio_a = 0.8;
        double ratio_b = 0.15;
        std::vector<std::pair<double, double>> expected_ranges = {{0.0, 0.0}, {30.5, 50.5}, {7.5, 8.5}, {5.5, 5.5}};
        std::map<double, std::vector<double>> expected_num_to_probs = {};
        Run<TYPE_DOUBLE>(input, pk_hash, sample_ratio, ratio_a, ratio_b, expected_ranges, expected_num_to_probs, false);
    }
}

TEST_F(CelonisBuildAbcModelTest, bigint_invalid_sample_ratio) {
    auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
    auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
    double sample_ratio = -1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.15;
    Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, {}, {}, true);
}

TEST_F(CelonisBuildAbcModelTest, bigint_invalid_ratio_a) {
    auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
    auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 1.8;
    double ratio_b = 0.15;
    Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, {}, {}, true);
}

TEST_F(CelonisBuildAbcModelTest, bigint_invalid_ratio_b) {
    auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
    auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = -0.5;
    Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, {}, {}, true);
}

TEST_F(CelonisBuildAbcModelTest, bigint_invalid_ratio_c) {
    auto input = DatumArray{50L, 30L, 8L, 7L, 5L};
    auto pk_hash = DatumArray{1L, 2L, 3L, 4L, 5L};
    double sample_ratio = 1.0;
    double ratio_a = 0.8;
    double ratio_b = 0.5;
    Run<TYPE_BIGINT>(input, pk_hash, sample_ratio, ratio_a, ratio_b, {}, {}, true);
}

} // namespace starrocks
