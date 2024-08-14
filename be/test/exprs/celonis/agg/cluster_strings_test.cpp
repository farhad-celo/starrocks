#include <algorithm>
#include <gtest/gtest.h>

#include "column/column_builder.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "../util.h"
#include "gutil/strings/strcat.h"
#include "testutil/function_utils.h"

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

} // namespace

class CelonisClusterStringsTest : public testing::Test {
public:
    CelonisClusterStringsTest() = default;

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        struct_type.children.emplace_back(celonis::array_type(TYPE_LARGEINT));
        struct_type.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
        struct_type.field_names.emplace_back("hash");
        struct_type.field_names.emplace_back("cluster_representative");
        return struct_type;
    }

    std::unique_ptr<FunctionContext> get_ctx() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),  // string
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT)), // hash
                AnyValUtil::column_type_to_type_desc(
                        TypeDescriptor::from_logical_type(TYPE_BIGINT)),   // edit_threshold
                AnyValUtil::column_type_to_type_desc(
                        TypeDescriptor::from_logical_type(TYPE_VARCHAR)),  // weighted_tokens
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))    // token_weight
        };
        auto return_type = AnyValUtil::column_type_to_type_desc(get_return_type());
        auto mem_pool = new MemPool();
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(nullptr, mem_pool, return_type, std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::vector<std::optional<std::string>>& strings, const std::vector<int128_t>& hashes,
              int64_t edit_threshold, const std::string& weighted_tokens, int64_t token_weight) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func = get_aggregate_function("celonis_cluster_strings", TYPE_VARCHAR, TYPE_STRUCT,
                                                               false);

        const auto size = strings.size();
        Columns columns;
        ColumnPtr string_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr hash_column = ColumnHelper::create_column(TypeDescriptor(TYPE_LARGEINT), true);
        for (auto i = 0; i < size; ++i) {
            if (strings[i].has_value()) {
                string_column->append_datum(Slice(strings[i].value()));
            } else {
                string_column->append_nulls(1);
            }
            hash_column->append_datum(hashes[i]);
        }
        columns.push_back(string_column);
        columns.push_back(hash_column);
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(edit_threshold, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(weighted_tokens, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(token_weight, size));

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

    void Evaluate(Column* result, const std::vector<std::pair<int128_t, std::optional<std::string>>>& expected) {
        auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(result))->fields_column();
        auto hashes_column = fields[0];
        auto strings_column = fields[1];
        ASSERT_EQ(1, hashes_column->size());
        ASSERT_EQ(1, strings_column->size());
        auto hash_array = hashes_column->get(0).get_array();
        auto string_array = strings_column->get(0).get_array();
        auto const length = expected.size();
        ASSERT_EQ(length, hash_array.size());
        ASSERT_EQ(length, string_array.size());
        std::vector<std::pair<int128_t, std::optional<std::string>>> hash_string_pairs;
        for (auto i = 0; i < length; ++i) {
            int128_t hash128 = hash_array[i].get_int128();
            std::optional<std::string> string_value = std::nullopt;
            if (!string_array[i].is_null()) {
                string_value = string_array[i].get_slice().to_string();
            }
            hash_string_pairs.emplace_back(hash128, string_value);
        }
        sort(hash_string_pairs.begin(), hash_string_pairs.end(),
             [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto i = 0; i < length; ++i) {
            EXPECT_EQ(expected[i].first, hash_string_pairs[i].first);
            EXPECT_EQ(expected[i].second, hash_string_pairs[i].second);
        }
    }

    void RunMerge(const std::vector<std::optional<std::string>>& strings1, const std::vector<int128_t>& hashes1,
                  const std::vector<std::optional<std::string>>& strings2, const std::vector<int128_t>& hashes2,
                  int64_t edit_threshold, const std::string& weighted_tokens, int64_t token_weight,
                  const std::vector<std::pair<int128_t, std::optional<std::string>>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(strings1, hashes1, edit_threshold, weighted_tokens, token_weight);
        auto [local_ctx2, state2, func2] = RunUpdate(strings2, hashes2, edit_threshold, weighted_tokens, token_weight);

        // Serialize state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        Evaluate(result.get(), expected);
        delete local_ctx1->mem_pool();
        delete local_ctx2->mem_pool();
    }

    void RunMergeToNew(const std::vector<std::optional<std::string>>& strings1, const std::vector<int128_t>& hashes1,
                       const std::vector<std::optional<std::string>>& strings2, const std::vector<int128_t>& hashes2,
                       int64_t edit_threshold, const std::string& weighted_tokens, int64_t token_weight,
                       const std::vector<std::pair<int128_t, std::optional<std::string>>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(strings1, hashes1, edit_threshold, weighted_tokens, token_weight);
        auto [local_ctx2, state2, func2] = RunUpdate(strings2, hashes2, edit_threshold, weighted_tokens, token_weight);
        auto local_ctx3 = get_ctx();

        // Serialize state1 and state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate(result.get(), expected);
        delete local_ctx1->mem_pool();
        delete local_ctx2->mem_pool();
        delete local_ctx3->mem_pool();
    }

    void Run(const std::vector<std::optional<std::string>>& strings1, const std::vector<int128_t>& hashes1,
             const std::vector<std::optional<std::string>>& strings2, const std::vector<int128_t>& hashes2,
             int64_t edit_threshold, const std::string& weighted_tokens, int64_t token_weight,
             const std::vector<std::pair<int128_t, std::optional<std::string>>>& expected) {
        RunMerge(strings1, hashes1, strings2, hashes2, edit_threshold, weighted_tokens, token_weight, expected);
        RunMergeToNew(strings1, hashes1, strings2, hashes2, edit_threshold, weighted_tokens, token_weight, expected);
    }

};

TEST_F(CelonisClusterStringsTest, simple) {
    std::vector<std::optional<std::string>> strings1 = {"chocolate", "cocolate"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"cholate", "chocolate", "chocolaet"};
    std::vector<int128_t> hashes2 = {3, 1, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "chocolate"},
                                                                             {2, "chocolate"},
                                                                             {3, "chocolate"},
                                                                             {4, "chocolate"}};

    Run(strings1, hashes1, strings2, hashes2, 2, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, alphabetical_order) {
    std::vector<std::optional<std::string>> strings1 = {"Terry Otter", "Harry Spotter"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"Harry Potter", "Larry Squatter"};
    std::vector<int128_t> hashes2 = {3, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "Harry Potter"},
                                                                             {2, "Harry Potter"},
                                                                             {3, "Harry Potter"},
                                                                             {4, "Harry Potter"}};

    Run(strings1, hashes1, strings2, hashes2, 4, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, weighted_string) {
    std::vector<std::optional<std::string>> strings1 = {"FLUX DIFUSER 9000", "FLUX DIFFUSER 9000"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"FLUX DIFFUSER 3000"};
    std::vector<int128_t> hashes2 = {3};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "FLUX DIFFUSER 9000"},
                                                                             {2, "FLUX DIFFUSER 9000"},
                                                                             {3, "FLUX DIFFUSER 3000"}};

    Run(strings1, hashes1, strings2, hashes2, 4, "0123456789", 10, expected);
}

TEST_F(CelonisClusterStringsTest, clustering_is_transitive) {
    std::vector<std::optional<std::string>> strings1 = {"ice cream", "ice ream", "ice eam"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"ice am", "ic am", "i am"};
    std::vector<int128_t> hashes2 = {4, 5, 6};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "i am"},
                                                                             {2, "i am"},
                                                                             {3, "i am"},
                                                                             {4, "i am"},
                                                                             {5, "i am"},
                                                                             {6, "i am"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, single_core) {
    std::vector<std::optional<std::string>> strings1 = {"ABC", "ADC"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"EBC"};
    std::vector<int128_t> hashes2 = {3};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "ABC"},
                                                                             {2, "ABC"},
                                                                             {3, "ABC"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, strings_no_overlap) {
    std::vector<std::optional<std::string>> strings1 = {"ABCDEFG", "HIGKLMN"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"UVWXYZOPQ"};
    std::vector<int128_t> hashes2 = {3};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "ABCDEFG"},
                                                                             {2, "HIGKLMN"},
                                                                             {3, "UVWXYZOPQ"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, null_values) {
    std::vector<std::optional<std::string>> strings1 = {std::nullopt, std::nullopt};
    std::vector<int128_t> hashes1 = {1, 1};
    std::vector<std::optional<std::string>> strings2 = {std::nullopt, std::nullopt};
    std::vector<int128_t> hashes2 = {1, 1};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, std::nullopt}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, null_with_non_null_values) {
    std::vector<std::optional<std::string>> strings1 = {std::nullopt, "abcd", std::nullopt};
    std::vector<int128_t> hashes1 = {1, 2, 1};
    std::vector<std::optional<std::string>> strings2 = {"abdd", std::nullopt, "abcde", std::nullopt};
    std::vector<int128_t> hashes2 = {3, 1, 4, 1};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, std::nullopt},
                                                                             {2, "abcd"},
                                                                             {3, "abcd"},
                                                                             {4, "abcd"}};

    Run(strings1, hashes1, strings2, hashes2, 3, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, filter_stable) {
    std::vector<std::optional<std::string>> strings1 = {"saola", "sara", "sahara"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"laola", "saola"};
    std::vector<int128_t> hashes2 = {4, 1};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "saola"},
                                                                             {2, "saola"},
                                                                             {3, "saola"},
                                                                             {4, "saola"}};

    Run(strings1, hashes1, strings2, hashes2, 2, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, linear_cluster) {
    std::vector<std::optional<std::string>> strings1 = {"ABC", "ABD"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"EBD", "EFD"};
    std::vector<int128_t> hashes2 = {3, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "ABC"},
                                                                             {2, "ABC"},
                                                                             {3, "ABC"},
                                                                             {4, "ABC"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, different_weight_and_length_1) {
    std::vector<std::optional<std::string>> strings1 = {"abdefghi", "abXY", "bdefgh"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"abd", "adef", "Xghijk", "X"};
    std::vector<int128_t> hashes2 = {4, 5, 6, 7};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "abd"},
                                                                             {2, "abXY"},
                                                                             {3, "abd"},
                                                                             {4, "abd"},
                                                                             {5, "abd"},
                                                                             {6, "X"},
                                                                             {7, "X"}};

    Run(strings1, hashes1, strings2, hashes2, 5, "abcXY", 3, expected);
}

TEST_F(CelonisClusterStringsTest, different_weight_and_length_2) {
    std::vector<std::optional<std::string>> strings1 = {"ABCD", "abdefg", "abc"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"bcde", "A"};
    std::vector<int128_t> hashes2 = {4, 5};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "A"},
                                                                             {2, "abc"},
                                                                             {3, "abc"},
                                                                             {4, "abc"},
                                                                             {5, "A"}};

    Run(strings1, hashes1, strings2, hashes2, 9, "abcXY", 3, expected);
}

TEST_F(CelonisClusterStringsTest, large_edit_threshold) {
    std::vector<std::optional<std::string>> strings1 = {"abdefghi", "abXY", "bdefgh"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"abd", "adef", "Xghijk", "X", "abd"};
    std::vector<int128_t> hashes2 = {4, 5, 6, 7, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "abd"},
                                                                             {2, "abd"},
                                                                             {3, "abd"},
                                                                             {4, "abd"},
                                                                             {5, "abd"},
                                                                             {6, "abd"},
                                                                             {7, "abd"}};

    Run(strings1, hashes1, strings2, hashes2, 16, "", 0, expected);
}

TEST_F(CelonisClusterStringsTest, unicode) {
    std::vector<std::optional<std::string>> strings1 = {"\u3082\u3076\u3089", "\u3082\u3077\u3089"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"\uFFD4\u3076\u3089"};
    std::vector<int128_t> hashes2 = {3};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "\u3082\u3076\u3089"},
                                                                             {2, "\u3082\u3076\u3089"},
                                                                             {3, "\u3082\u3076\u3089"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, empty_input) {
    std::vector<std::optional<std::string>> strings1 = {};
    std::vector<int128_t> hashes1 = {};
    std::vector<std::optional<std::string>> strings2 = {};
    std::vector<int128_t> hashes2 = {};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {};

    Run(strings1, hashes1, strings2, hashes2, 1, "", 1, expected);
}

TEST_F(CelonisClusterStringsTest, negative_token_weight) {
    std::vector<std::optional<std::string>> strings1 = {"abdefghi", "abXY", "bdefgh"};
    std::vector<int128_t> hashes1 = {1, 2, 3};
    std::vector<std::optional<std::string>> strings2 = {"abd", "adef", "Xghijk", "X"};
    std::vector<int128_t> hashes2 = {4, 5, 6, 7};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {};
    Run(strings1, hashes1, strings2, hashes2, 5, "abcXY", -1, expected);
}

TEST_F(CelonisClusterStringsTest, simple_case_1) {
    std::vector<std::optional<std::string>> strings1 = {"Pizza", "Pisza", "Pizza"};
    std::vector<int128_t> hashes1 = {1, 2, 1};
    std::vector<std::optional<std::string>> strings2 = {"Pitza", "Bizza"};
    std::vector<int128_t> hashes2 = {3, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "Pizza"},
                                                                             {2, "Pizza"},
                                                                             {3, "Pizza"},
                                                                             {4, "Pizza"}};
    Run(strings1, hashes1, strings2, hashes2, 2, "", 0, expected);
}

TEST_F(CelonisClusterStringsTest, simple_case_2) {
    std::vector<std::optional<std::string>> strings1 = {"AA", "AB"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"CD"};
    std::vector<int128_t> hashes2 = {3};
    // "CD" is not in the same cluster as "AA", because they do not share any chars.
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "AA"},
                                                                             {2, "AA"},
                                                                             {3, "CD"}};
    Run(strings1, hashes1, strings2, hashes2, 2, "", 0, expected);
}

TEST_F(CelonisClusterStringsTest, zero_token_weight) {
    std::vector<std::optional<std::string>> strings1 = {"1AA", "A2A"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"AA34567", "A789A123"};
    std::vector<int128_t> hashes2 = {3, 4};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "1AA"},
                                                                             {2, "1AA"},
                                                                             {3, "1AA"},
                                                                             {4, "1AA"}};
    // since the cost of digits is zero, all the strings belong to the same cluster.
    Run(strings1, hashes1, strings2, hashes2, 0, "0123456789", 0, expected);
}

TEST_F(CelonisClusterStringsTest, unicode_weighted_tokens) {
    std::vector<std::optional<std::string>> strings1 = {"\u3082\u3076\u3089", "\u3082\u3077\u3089"};
    std::vector<int128_t> hashes1 = {1, 2};
    std::vector<std::optional<std::string>> strings2 = {"\uFFD4\u3076\u3089"};
    std::vector<int128_t> hashes2 = {3};
    std::vector<std::pair<int128_t, std::optional<std::string>>> expected = {{1, "\u3082\u3076\u3089"},
                                                                             {2, "\u3082\u3076\u3089"},
                                                                             {3, "\uFFD4\u3076\u3089"}};

    Run(strings1, hashes1, strings2, hashes2, 1, "xyz\uFFD4", 10, expected);
}

} // namespace starrocks