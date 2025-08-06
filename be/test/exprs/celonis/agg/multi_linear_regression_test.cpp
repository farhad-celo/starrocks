#include <algorithm>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/struct_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/multi_linear_regression.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

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

class CelonisBuildMultiLinearRegressionModelTest : public testing::Test {
protected:
    CelonisBuildMultiLinearRegressionModelTest() = default;

    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        TypeDescriptor type_return_varchar;
        type_return_varchar.type = LogicalType::TYPE_VARCHAR;
        return type_return_varchar;
    }

    std::unique_ptr<FunctionContext> get_ctx(LogicalType logical_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(celonis::array_type(logical_type)),               // x
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(logical_type))  // y
        };
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(nullptr, mem_pools_.back().get(), return_type, std::move(arg_types)));
    }

    std::optional<std::vector<double>>
    ComputeExpectedBeta(const std::vector<DatumArray>& xs, const std::vector<DatumArray>& ys, bool is_bigint) {
        DCHECK_EQ(xs.size(), ys.size());
        DatumArray x;
        DatumArray y;
        const auto size = xs.size();
        for (auto i = 0; i < size; ++i) {
            const auto& cur_x = xs[i];
            const auto& cur_y = ys[i];
            DCHECK_EQ(cur_x.size(), cur_y.size());
            for (auto j = 0; j < cur_x.size(); ++j) {
                if (cur_x[j].is_null() || cur_y[j].is_null()) {
                    continue;
                }
                auto array = cur_x[j].get_array();
                bool has_null = false;
                for (auto k = 0; k < array.size(); ++k) {
                    if (array[k].is_null()) {
                        has_null = true;
                        break;
                    }
                }
                if (has_null) {
                    continue;
                }
                x.push_back(cur_x[j]);
                y.push_back(cur_y[j]);
            }
        }
        if (y.empty()) {
            return std::nullopt;
        }
        const auto n_samples = y.size();
        const auto n_features = x[0].get_array().size();
        boost::numeric::ublas::matrix<double> X(n_samples, n_features + 1);
        boost::numeric::ublas::vector<double> Y(n_samples);
        boost::numeric::ublas::vector<double> beta(n_features + 1);
        for (auto i = 0; i < n_samples; ++i) {
            X(i, 0) = 1.0;
            for (auto j = 0; j < n_features; ++j) {
                if (is_bigint) {
                    X(i, j + 1) = x[i].get_array()[j].get_int64();
                } else {
                    X(i, j + 1) = x[i].get_array()[j].get_double();
                }
            }
            if (is_bigint) {
                Y(i) = y[i].get_int64();
            } else {
                Y(i) = y[i].get_double();
            }
        }
        // compute (X^T * X)
        boost::numeric::ublas::matrix<double> XtX = boost::numeric::ublas::prod(boost::numeric::ublas::trans(X), X);

        // compute (X^T * y)
        boost::numeric::ublas::vector<double> Xty = boost::numeric::ublas::prod(boost::numeric::ublas::trans(X), Y);
        // solve for beta using LU decomposition
        if (lu_solve(XtX, Xty, beta)) {
            std::vector<double> result(beta.size());
            for (size_t i = 0; i < beta.size(); ++i) {
                result[i] = beta[i];
            }
            return result;
        } else {
            return std::nullopt;
        }
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(LogicalType logical_type, const DatumArray& x, const DatumArray& y) {
        auto local_ctx = get_ctx(logical_type);

        const AggregateFunction* func =
                get_aggregate_function("celonis_build_multi_linear_regression_model", TYPE_ARRAY, TYPE_VARCHAR,
                                       false);

        auto x_col = ColumnHelper::create_column(celonis::array_type(logical_type), true);
        for (const auto& datum: x) {
            x_col->append_datum(datum);
        }
        auto y_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(logical_type), true);
        for (const auto& datum: y) {
            y_col->append_datum(datum);
        }
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_col.get();
        raw_columns[1] = y_col.get();
        local_ctx->set_constant_columns({nullptr, nullptr});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), x.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    void ValidateModel(const std::vector<double>& expected_beta, const std::string& model) {
        std::vector<double> beta;
        ASSERT_TRUE(CreateModel(model, beta));
        ASSERT_EQ(expected_beta.size(), beta.size());
        for (auto i = 0; i < expected_beta.size(); ++i) {
            if (std::isnan(expected_beta[i])) {
                EXPECT_TRUE(std::isnan(beta[i]));
            } else {
                EXPECT_NEAR(expected_beta[i], beta[i], ABS_ERROR);
            }
        }
    }

    bool CreateModel(const std::string& model, std::vector<double>& beta) {
        std::vector<std::string> parts;
        boost::split(parts, model, boost::is_any_of(":"));
        if (parts.size() < 2) {
            return false;
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            try {
                auto value = boost::lexical_cast<double>(parts[i]);
                beta.push_back(value);
            } catch (const boost::bad_lexical_cast& e) {
                return false;
            }
        }
        return true;
    }

    template<LogicalType LT>
    void Run(const DatumArray& x, const DatumArray& y, const std::vector<double>& expected_beta, bool is_null = false) {
        auto [local_ctx, state, func] = RunUpdate(LT, x, y);

        auto result = ColumnHelper::create_column(get_return_type(), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());
        ASSERT_EQ(1, result->size());
        if (is_null) {
            ASSERT_TRUE(local_ctx->has_error());
            const char* error = local_ctx->error_msg();
            ASSERT_NE(error, nullptr);
            EXPECT_EQ(std::string_view(error),
                      "CELONIS_BUILD_MULTI_LINEAR_REGRESSION_MODEL: Unable to fit regression model. The system is singular or ill-conditioned.");
            EXPECT_TRUE(result->get(0).is_null());
        } else {
            const std::string model = result->get(0).get_slice().to_string();
            ValidateModel(expected_beta, model);
        }
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
};

TEST_F(CelonisBuildMultiLinearRegressionModelTest, bigint_single_dimension_merge) {
    auto logical_type = TYPE_DOUBLE;
    auto x1 = DatumArray{DatumArray{1.0}, DatumArray{1.0}, DatumArray{2.0}};
    auto x2 = DatumArray{DatumArray{3.0}, DatumArray{4.0}};
    auto y1 = DatumArray{100.0, 300.0, 400.0};
    auto y2 = DatumArray{300.0, 500.0};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, x1, y1);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, x2, y2);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    auto expected_beta = ComputeExpectedBeta({x1, x2}, {y1, y2}, false);
    ASSERT_TRUE(expected_beta.has_value());
    ValidateModel(expected_beta.value(), result->get(0).get_slice().to_string());
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, double_multi_dimensions_merge) {
    auto logical_type = TYPE_DOUBLE;
    std::vector<double> x1s = {2.75, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.25, 2.25, 2.25, 2, 2, 2, 1.75, 1.75, 1.75,
                               1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75};
    std::vector<double> x2s = {5.3, 5.3, 5.3, 5.3, 5.4, 5.6, 5.5, 5.5, 5.5, 5.6, 5.7, 5.9, 6, 5.9, 5.8, 6.1, 6.2,
                               6.1, 6.1, 6.1, 5.9, 6.2, 6.2, 6.1};
    std::vector<double> ys = {1464, 1394, 1357, 1293, 1256, 1254, 1234, 1195, 1159, 1167, 1130, 1075, 1047, 965,
                              943, 958, 971, 949, 884, 866, 876, 822, 704, 719};
    auto n = ys.size();
    auto x1 = DatumArray{};
    auto x2 = DatumArray{};
    auto y1 = DatumArray{};
    auto y2 = DatumArray{};
    for (auto i = 0; i < n / 2; ++i) {
        x1.push_back(DatumArray{x1s[i], x2s[i]});
        y1.push_back(ys[i]);
    }
    for (auto i = n / 2; i < n; ++i) {
        x2.push_back(DatumArray{x1s[i], x2s[i]});
        y2.push_back(ys[i]);
    }

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, x1, y1);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, x2, y2);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    auto expected_beta = ComputeExpectedBeta({x1, x2}, {y1, y2}, false);
    ASSERT_TRUE(expected_beta.has_value());
    ValidateModel(expected_beta.value(), result->get(0).get_slice().to_string());
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, 3_features) {
    auto x = DatumArray{DatumArray{1.0, 2.0, 3.0}, DatumArray{1.0, 2.0, 3.0}, DatumArray{2.0, 3.0, 4.0}, DatumArray{3.0, 4.0, 5.0},
                        DatumArray{4.0, 5.0, 6.0}};
    auto y = DatumArray{100.0, 300.0, 400.0, 300.0, 500.0};
    auto expected_beta = ComputeExpectedBeta({x}, {y}, false);
    ASSERT_TRUE(expected_beta.has_value());
    Run<TYPE_DOUBLE>(x, y, expected_beta.value(), false);
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, double_run) {
    std::vector<double> x1s = {2.75, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.25, 2.25, 2.25, 2, 2, 2, 1.75, 1.75, 1.75,
                               1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75};
    std::vector<double> x2s = {5.3, 5.3, 5.3, 5.3, 5.4, 5.6, 5.5, 5.5, 5.5, 5.6, 5.7, 5.9, 6, 5.9, 5.8, 6.1, 6.2,
                               6.1, 6.1, 6.1, 5.9, 6.2, 6.2, 6.1};
    std::vector<double> ys = {1464, 1394, 1357, 1293, 1256, 1254, 1234, 1195, 1159, 1167, 1130, 1075, 1047, 965,
                              943, 958, 971, 949, 884, 866, 876, 822, 704, 719};
    auto x = DatumArray{};
    auto y = DatumArray{};
    for (auto i = 0; i < ys.size(); ++i) {
        x.push_back(DatumArray{x1s[i], x2s[i]});
        y.push_back(ys[i]);
    }
    auto expected_beta = ComputeExpectedBeta({x}, {y}, false);
    ASSERT_TRUE(expected_beta.has_value());
    Run<TYPE_DOUBLE>(x, y, expected_beta.value(), false);
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, null_x_or_y) {
    auto x = DatumArray{kNullDatum, DatumArray{1.0}, DatumArray{1.0}, kNullDatum, DatumArray{2.0}, DatumArray{3.0},
                        DatumArray{4.0}, DatumArray{5.0}};
    auto y = DatumArray{10.0, 100.0, 300.0, kNullDatum, 400.0, 300.0, 500.0, kNullDatum};
    auto expected_beta = ComputeExpectedBeta({x}, {y}, false);
    ASSERT_TRUE(expected_beta.has_value());
    Run<TYPE_DOUBLE>(x, y, expected_beta.value(), false);
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, null_in_x_array) {
    auto x = DatumArray{DatumArray{1.0}, DatumArray{1.0}, DatumArray{3.0, kNullDatum}, DatumArray{2.0}, DatumArray{3.0},
                        DatumArray{4.0}};
    auto y = DatumArray{100.0, 300.0, 200.0, 400.0, 300.0, 500.0};
    auto expected_beta = ComputeExpectedBeta({x}, {y}, false);
    ASSERT_TRUE(expected_beta.has_value());
    Run<TYPE_DOUBLE>(x, y, expected_beta.value(), false);
}

TEST_F(CelonisBuildMultiLinearRegressionModelTest, not_enough_data_points) {
    auto x = DatumArray{DatumArray{1.0}};
    auto y = DatumArray{100.0};
    Run<TYPE_DOUBLE>(x, y, {}, true);
}

} // namespace starrocks
