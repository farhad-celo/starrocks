#include <algorithm>
#include <gtest/gtest.h>

#include "column/column_builder.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "modules/query/variantstats.pb.h"
#include "exprs/anyval_util.h"
#include "runtime/runtime_state.h"
#include "../util.h"
#include "google/protobuf/util/json_util.h"
#include "gutil/strings/strcat.h"
#include "testutil/function_utils.h"

namespace starrocks {

namespace {

std::optional<std::string> to_statistics_json_string(const std::string& encoded_string) {
    int cipher_len = encoded_string.length();
    std::unique_ptr<char[]> p;
    p.reset(new char[cipher_len + 3]);

    int len = base64_decode3(encoded_string.data(), encoded_string.length(), p.get());
    std::string decoded_string(p.get(), len);
    ::celonis::accelerator::Statistics statistics_proto;
    bool success = statistics_proto.ParseFromString(decoded_string);
    if (!success) {
        return std::nullopt;
    }
    std::string statistics_json;
    google::protobuf::util::MessageToJsonString(statistics_proto, &statistics_json);
    return statistics_json;
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

} // namespace

class CelonisVariantStatsV2Test : public testing::Test {
public:
    CelonisVariantStatsV2Test() = default;

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);

protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        return TypeDescriptor::from_logical_type(TYPE_VARCHAR);
    }

    std::unique_ptr<FunctionContext> get_ctx() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(TYPE_ARRAY),     // variant
                TypeDescriptor::from_logical_type(TYPE_BIGINT),    // count
                TypeDescriptor::from_logical_type(TYPE_ARRAY),     // activity_array
                TypeDescriptor::from_logical_type(TYPE_BIGINT),    // edge_count
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN),   // disable_top_variant_stats
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN)    // enable_proto_encoding
        };
        auto return_type = TypeDescriptor::from_logical_type(TYPE_VARCHAR);
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        runtime_states_.emplace_back(std::make_unique<RuntimeState>());
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(runtime_states_.back().get(), mem_pools_.back().get(), return_type,
                                                std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::vector<std::optional<DatumArray>>& variants, const std::vector<int64_t>& counts,
              const DatumArray& activity_array, int64_t edge_count, bool disable_top_variant_stats,
              bool enable_proto_encoding) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func = get_aggregate_function("celonis_variant_stats_v2", TYPE_ARRAY, TYPE_VARCHAR,
                                                               false);
        DCHECK(func != nullptr);

        const auto size = variants.size();
        DCHECK_EQ(size, counts.size());
        Columns columns;
        ColumnPtr variant_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_INT), true);
        ColumnPtr count_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        ColumnPtr activity_array_column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        activity_array_column->append_datum(activity_array);
        activity_array_column = ConstColumn::create(activity_array_column, size);
        for (auto i = 0; i < size; ++i) {
            if (variants[i].has_value()) {
                variant_column->append_datum(variants[i].value());
            } else {
                variant_column->append_nulls(1);
            }
            count_column->append_datum(counts[i]);
        }
        columns.push_back(variant_column);
        columns.push_back(count_column);
        columns.push_back(activity_array_column);
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(edge_count, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_BOOLEAN>(disable_top_variant_stats, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_BOOLEAN>(enable_proto_encoding, size));

        std::vector<ColumnPtr> const_columns;
        std::vector<const Column*> raw_columns;
        for (auto& column: columns) {
            if (column->is_constant()) {
                const_columns.push_back(column);
            } else {
                const_columns.push_back(nullptr);
            }
            raw_columns.push_back(column.get());
        }

        local_ctx->set_constant_columns(std::move(const_columns));

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), size, raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    void Evaluate(Column* result, const std::vector<std::string>& expected, bool enable_proto_encoding) {
        ASSERT_EQ(expected.size(), result->size());
        for (auto i = 0; i < expected.size(); ++i) {
            if (enable_proto_encoding) {
                EXPECT_EQ(expected[i], to_statistics_json_string(result->get(i).get_slice().to_string()).value());
            } else {
                EXPECT_EQ(expected[i], result->get(i).get_slice().to_string());
            }
        }
    }

    void RunMerge(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int64_t>& counts1,
                  const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int64_t>& counts2,
                  const DatumArray& activity_array, int64_t edge_count, bool disable_top_variant_stats,
                  bool enable_proto_encoding, const std::vector<std::string>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(variants1, counts1, activity_array, edge_count,
                                                    disable_top_variant_stats, enable_proto_encoding);
        auto [local_ctx2, state2, func2] = RunUpdate(variants2, counts2, activity_array, edge_count,
                                                     disable_top_variant_stats, enable_proto_encoding);

        // Serialize state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        // std::cerr << result->debug_string() << std::endl;
        Evaluate(result.get(), expected, enable_proto_encoding);
    }

    void RunMergeNew(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int64_t>& counts1,
                     const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int64_t>& counts2,
                     const DatumArray& activity_array, int64_t edge_count, bool disable_top_variant_stats,
                     bool enable_proto_encoding, const std::vector<std::string>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(variants1, counts1, activity_array, edge_count,
                                                    disable_top_variant_stats, enable_proto_encoding);
        auto [local_ctx2, state2, func2] = RunUpdate(variants2, counts2, activity_array, edge_count,
                                                     disable_top_variant_stats, enable_proto_encoding);
        // Serialize state1 and state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto local_ctx3 = get_ctx();
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);

        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate(result.get(), expected, enable_proto_encoding);
    }

    void Run(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int64_t>& counts1,
             const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int64_t>& counts2,
             const DatumArray& activity_array, int64_t edge_count, bool disable_top_variant_stats,
             bool enable_proto_encoding, const std::vector<std::string>& expected) {
        RunMerge(variants1, counts1, variants2, counts2, activity_array, edge_count, disable_top_variant_stats,
                 enable_proto_encoding, expected);
        RunMergeNew(variants1, counts1, variants2, counts2, activity_array, edge_count, disable_top_variant_stats,
                    enable_proto_encoding, expected);
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
    std::vector<std::unique_ptr<RuntimeState>> runtime_states_;
};

// TODO(y.zhang): Add more unit tests.
TEST_F(CelonisVariantStatsV2Test, use_32bits_activity_work) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {};
    std::vector<std::string> activities;
    size_t n = static_cast<size_t>(std::numeric_limits<int16_t>::max()) + 10;
    activities.resize(n);
    for (size_t i = 0; i < n; ++i) {
        activities.push_back(std::to_string(i));
    }
    for (const auto& activity: activities) {
        activity_array.emplace_back(Slice(activity));
    }
    // Currently finalize_to_column directly returns if activity_array().size() > std::numeric_limits<int16_t>::max().
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {""""""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {""""""});
}

TEST_F(CelonisVariantStatsV2Test, normal_case) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, normal_case_with_empty_variants) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}, DatumArray{}};
    std::vector<int64_t> counts1 = {2, 1};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}, DatumArray{}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, duplicate_activities_in_activity_array) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}, DatumArray{}};
    std::vector<int64_t> counts1 = {2, 1};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}, DatumArray{}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "A", "C", "B", kNullDatum, "D", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, duplicate_edges) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0, 1, 2, 1, 2}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":0,\"id\":0},{\"count\":7,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":7,\"count_case\":5,\"count_start\":1,\"count_end\":3,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":3,\"count_case\":3,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":6,\"count_case\":5,\"src\":1,\"dst\":2},{\"count\":2,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":0,\"id\":0},{\"count\":7,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":7,\"count_case\":5,\"count_start\":1,\"count_end\":3,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":3,\"count_case\":3,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":6,\"count_case\":5,\"src\":1,\"dst\":2},{\"count\":2,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, normal_case_with_top_variant_stats) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, false, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"top\":[{\"id\":0,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":1,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[1,2],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":2,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[1,2],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":3,\"top\":[{\"variant\":[0,1,2,3],\"count\":2}]}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, false, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"top\":[{\"id\":0,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":1,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[1,2],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":2,\"top\":[{\"variant\":[0,1,2,3],\"count\":2},{\"variant\":[1,2],\"count\":2},{\"variant\":[2,1,0],\"count\":1}]},{\"id\":3,\"top\":[{\"variant\":[0,1,2,3],\"count\":2}]}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, normal_case_without_edge_stats) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, -1, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_stats\":[],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, -1, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_stats\":[],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, proto_encoding_enabled) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, true,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"eCount\":\"5\",\"eStats\":[{\"count\":\"2\",\"countCase\":\"2\",\"src\":0,\"dst\":1},{\"count\":\"1\",\"countCase\":\"1\",\"src\":1,\"dst\":0},{\"count\":\"4\",\"countCase\":\"4\",\"src\":1,\"dst\":2},{\"count\":\"1\",\"countCase\":\"1\",\"src\":2,\"dst\":1},{\"count\":\"2\",\"countCase\":\"2\",\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, true,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"eCount\":\"5\",\"eStats\":[{\"count\":\"2\",\"countCase\":\"2\",\"src\":0,\"dst\":1},{\"count\":\"1\",\"countCase\":\"1\",\"src\":1,\"dst\":0},{\"count\":\"4\",\"countCase\":\"4\",\"src\":1,\"dst\":2},{\"count\":\"1\",\"countCase\":\"1\",\"src\":2,\"dst\":1},{\"count\":\"2\",\"countCase\":\"2\",\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
}

TEST_F(CelonisVariantStatsV2Test, proto_encoding_enabled_with_top_variant_stats) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, true,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"eCount\":\"5\",\"eStats\":[{\"count\":\"2\",\"countCase\":\"2\",\"src\":0,\"dst\":1},{\"count\":\"1\",\"countCase\":\"1\",\"src\":1,\"dst\":0},{\"count\":\"4\",\"countCase\":\"4\",\"src\":1,\"dst\":2},{\"count\":\"1\",\"countCase\":\"1\",\"src\":2,\"dst\":1},{\"count\":\"2\",\"countCase\":\"2\",\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, true,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"eCount\":\"5\",\"eStats\":[{\"count\":\"2\",\"countCase\":\"2\",\"src\":0,\"dst\":1},{\"count\":\"1\",\"countCase\":\"1\",\"src\":1,\"dst\":0},{\"count\":\"4\",\"countCase\":\"4\",\"src\":1,\"dst\":2},{\"count\":\"1\",\"countCase\":\"1\",\"src\":2,\"dst\":1},{\"count\":\"2\",\"countCase\":\"2\",\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
}

TEST_F(CelonisVariantStatsV2Test, proto_encoding_enabled_without_edge_stats) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}};
    std::vector<int64_t> counts2 = {1, 2};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, -1, true, true,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, -1, true, true,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"aStats\":[{\"count\":\"3\",\"countCase\":\"3\",\"countStart\":\"2\",\"countEnd\":\"1\",\"id\":0},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"2\",\"countEnd\":\"0\",\"id\":1},{\"count\":\"5\",\"countCase\":\"5\",\"countStart\":\"1\",\"countEnd\":\"2\",\"id\":2},{\"count\":\"2\",\"countCase\":\"2\",\"countStart\":\"0\",\"countEnd\":\"2\",\"id\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":\"2\"}}"""});
}

TEST_F(CelonisVariantStatsV2Test, bad_encoded_variants_are_dropped) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}, DatumArray{-1, 0, 1, 2, 3}};
    std::vector<int64_t> counts1 = {2, 2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}, DatumArray{0, 10, 1}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, null_variants_are_dropped) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{0, 1, 2, 3}, DatumArray{-1, 0, 1, 2, 3},
                                                        std::nullopt};
    std::vector<int64_t> counts1 = {2, 2, 1};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{2, 1, 0}, DatumArray{1, 2}, DatumArray{0, 10, 1}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
             {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false,
                {"""{\"dict\":[{\"id\":0,\"name\":\"A\"},{\"id\":1,\"name\":\"B\"},{\"id\":2,\"name\":\"C\"},{\"id\":3,\"name\":\"D\"}],\"a_stats\":[{\"count\":3,\"count_case\":3,\"count_start\":2,\"count_end\":1,\"id\":0},{\"count\":5,\"count_case\":5,\"count_start\":2,\"count_end\":0,\"id\":1},{\"count\":5,\"count_case\":5,\"count_start\":1,\"count_end\":2,\"id\":2},{\"count\":2,\"count_case\":2,\"count_start\":0,\"count_end\":2,\"id\":3}],\"e_count\":5,\"e_stats\":[{\"count\":2,\"count_case\":2,\"src\":0,\"dst\":1},{\"count\":1,\"count_case\":1,\"src\":1,\"dst\":0},{\"count\":4,\"count_case\":4,\"src\":1,\"dst\":2},{\"count\":1,\"count_case\":1,\"src\":2,\"dst\":1},{\"count\":2,\"count_case\":2,\"src\":2,\"dst\":3}],\"happy\":{\"variant\":[0,1,2,3],\"count\":2}}"""});
}

TEST_F(CelonisVariantStatsV2Test, no_valid_variants) {
    // no variants are valid
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{-10, 1, 2, 3}, DatumArray{-1, 0, 1, 2, 3},
                                                        std::nullopt};
    std::vector<int64_t> counts1 = {2, 2, 1};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{100, 1, 0}, DatumArray{-3, 2}, DatumArray{0, 10, 1}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {"{}"});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {"{}"});
}

TEST_F(CelonisVariantStatsV2Test, proto_encoding_enabled_with_no_valid_variants) {
    // no variants are valid
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{-10, 1, 2, 3}, DatumArray{-1, 0, 1, 2, 3},
                                                        std::nullopt};
    std::vector<int64_t> counts1 = {2, 2, 1};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{100, 1, 0}, DatumArray{-3, 2}, DatumArray{0, 10, 1}};
    std::vector<int64_t> counts2 = {1, 2, 1};
    DatumArray activity_array = {"A", "B", "C", "D"};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, true, {"{}"});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, true, {"{}"});
}

TEST_F(CelonisVariantStatsV2Test, no_activities) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{}};
    std::vector<int64_t> counts2 = {3};
    DatumArray activity_array = {};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {"{}"});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, false, {"{}"});
}

TEST_F(CelonisVariantStatsV2Test, proto_encoding_enabled_with_no_activities) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{}};
    std::vector<int64_t> counts1 = {2};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{}};
    std::vector<int64_t> counts2 = {3};
    DatumArray activity_array = {};
    RunMerge(variants1, counts1, variants2, counts2, activity_array, 1000, true, true, {"{}"});
    RunMergeNew(variants1, counts1, variants2, counts2, activity_array, 1000, true, true, {"{}"});
}

} // namespace starrocks
