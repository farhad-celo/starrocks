#include "exprs/celonis/create_alignment.h"

#include <ctl/type_traits.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <random>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

#include "bpmn_models.h"
#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_types_proxy.h"
#include "exprs/celonis/result_table.h"
#include "runtime/mem_pool.h"
#include "testutil/assert.h"
#include "testutil/function_utils.h"
#include "util.h"
#include "util/defer_op.h"
#include "util/slice.h"

namespace starrocks {

#define EDGE_TYPES(VIOLATION_NAME)                                                                      \
    std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<std::string>, \
            std::vector<int64_t>, std::vector<int64_t>

class CelonisCreateAlignmentTest : public testing::Test {
public:
    typedef std::vector<std::string> Variant;
    typedef std::vector<Variant> VariantRows;
    typedef std::tuple<std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<std::string>, EDGE_TYPES("SYNC"), EDGE_TYPES("MODEL"), EDGE_TYPES("SKIP"),
                       EDGE_TYPES("LOG"), EDGE_TYPES("UNMAPPED"), EDGE_TYPES("MISSING"),
                       EDGE_TYPES("EXCLUSIVE_VIOLATION")>
            Result;
    using ResultMap = std::map<Variant, Result>;

    //TODO(j.kruska) At some point it would be nice to have a better builder and comparator here.
    // Right now the builder and comparator is still ordered, i.e. edges need to be added to the builder in the same order they are produced by the algorithm.
    // Then the resulting arrays are compared with the order.
    // Actually only the order within each edge components matters here and should be tested.
    // The order in which order these components are produced is an implementation detail that does not need to be tested.
    // (Meaning if we change the implementation to produce a different order the test would fail, despite all guarantees of the operator still holding)
    class CreateAlignmentTestResultBuilder {
    public:
        using edge_fields_t = std::tuple<std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>,
                                         std::vector<std::string>, std::vector<int64_t>, std::vector<int64_t>>;

        CreateAlignmentTestResultBuilder() = default;

        CreateAlignmentTestResultBuilder& alignment_model_vertex_id(std::vector<int64_t> value) {
            alignment_model_vertex_id_ = std::move(value);
            return *this;
        }

        CreateAlignmentTestResultBuilder& alignment_vertex_label(std::vector<std::string> value) {
            alignment_vertex_label_ = std::move(value);
            return *this;
        }

        CreateAlignmentTestResultBuilder& alignment_move_type(std::vector<std::string> value) {
            alignment_move_type_ = std::move(value);
            return *this;
        }

        CreateAlignmentTestResultBuilder& alignment_activity_index(std::vector<int64_t> value) {
            alignment_activity_index_ = std::move(value);
            return *this;
        }

        CreateAlignmentTestResultBuilder& alignment_deviation_category(std::vector<std::string> value) {
            alignment_deviation_category_ = std::move(value);
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_sync_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            sync_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_model_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            model_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_skip_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            skip_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_log_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            log_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_unmapped_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            unmapped_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_missing_violation_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            missing_violation_edges_.emplace_back(std::move(edge));
            return *this;
        }

        CreateAlignmentTestResultBuilder& add_exclusive_violation_edge(edge_fields_t edge) {
            EXPECT_TRUE(field_lengths_match(edge));
            exclusive_violation_edges_.emplace_back(std::move(edge));
            return *this;
        }

        Result build() const {
            auto flattened_sync_edges{flatten_edge_fields(sync_edges_)};
            auto flattened_model_edges{flatten_edge_fields(model_edges_)};
            auto flattened_skip_edges{flatten_edge_fields(skip_edges_)};
            auto flattened_log_edges{flatten_edge_fields(log_edges_)};
            auto flattened_unmapped_edges{flatten_edge_fields(unmapped_edges_)};
            auto flattened_missing_violation_edges{flatten_edge_fields(missing_violation_edges_)};
            auto flattened_exclusive_violation_edges{flatten_edge_fields(exclusive_violation_edges_)};
            return {std::move(alignment_model_vertex_id_),
                    std::move(alignment_vertex_label_),
                    std::move(alignment_move_type_),
                    std::move(alignment_activity_index_),
                    std::move(alignment_deviation_category_),
                    std::move(std::get<0>(flattened_sync_edges)),
                    std::move(std::get<1>(flattened_sync_edges)),
                    std::move(std::get<2>(flattened_sync_edges)),
                    std::move(std::get<3>(flattened_sync_edges)),
                    std::move(std::get<4>(flattened_sync_edges)),
                    std::move(std::get<5>(flattened_sync_edges)),
                    std::move(std::get<0>(flattened_model_edges)),
                    std::move(std::get<1>(flattened_model_edges)),
                    std::move(std::get<2>(flattened_model_edges)),
                    std::move(std::get<3>(flattened_model_edges)),
                    std::move(std::get<4>(flattened_model_edges)),
                    std::move(std::get<5>(flattened_model_edges)),
                    std::move(std::get<0>(flattened_skip_edges)),
                    std::move(std::get<1>(flattened_skip_edges)),
                    std::move(std::get<2>(flattened_skip_edges)),
                    std::move(std::get<3>(flattened_skip_edges)),
                    std::move(std::get<4>(flattened_skip_edges)),
                    std::move(std::get<5>(flattened_skip_edges)),
                    std::move(std::get<0>(flattened_log_edges)),
                    std::move(std::get<1>(flattened_log_edges)),
                    std::move(std::get<2>(flattened_log_edges)),
                    std::move(std::get<3>(flattened_log_edges)),
                    std::move(std::get<4>(flattened_log_edges)),
                    std::move(std::get<5>(flattened_log_edges)),
                    std::move(std::get<0>(flattened_unmapped_edges)),
                    std::move(std::get<1>(flattened_unmapped_edges)),
                    std::move(std::get<2>(flattened_unmapped_edges)),
                    std::move(std::get<3>(flattened_unmapped_edges)),
                    std::move(std::get<4>(flattened_unmapped_edges)),
                    std::move(std::get<5>(flattened_unmapped_edges)),
                    std::move(std::get<0>(flattened_missing_violation_edges)),
                    std::move(std::get<1>(flattened_missing_violation_edges)),
                    std::move(std::get<2>(flattened_missing_violation_edges)),
                    std::move(std::get<3>(flattened_missing_violation_edges)),
                    std::move(std::get<4>(flattened_missing_violation_edges)),
                    std::move(std::get<5>(flattened_missing_violation_edges)),
                    std::move(std::get<0>(flattened_exclusive_violation_edges)),
                    std::move(std::get<1>(flattened_exclusive_violation_edges)),
                    std::move(std::get<2>(flattened_exclusive_violation_edges)),
                    std::move(std::get<3>(flattened_exclusive_violation_edges)),
                    std::move(std::get<4>(flattened_exclusive_violation_edges)),
                    std::move(std::get<5>(flattened_exclusive_violation_edges))};
        }

    private:
        static edge_fields_t flatten_edge_fields(const std::vector<edge_fields_t>& edges) {
            edge_fields_t flattened;
            for (const auto& edge : edges) {
                std::get<0>(flattened).insert(std::get<0>(flattened).end(), std::get<0>(edge).begin(),
                                              std::get<0>(edge).end());
                std::get<1>(flattened).insert(std::get<1>(flattened).end(), std::get<1>(edge).begin(),
                                              std::get<1>(edge).end());
                std::get<2>(flattened).insert(std::get<2>(flattened).end(), std::get<2>(edge).begin(),
                                              std::get<2>(edge).end());
                std::get<3>(flattened).insert(std::get<3>(flattened).end(), std::get<3>(edge).begin(),
                                              std::get<3>(edge).end());
                std::get<4>(flattened).insert(std::get<4>(flattened).end(), std::get<4>(edge).begin(),
                                              std::get<4>(edge).end());
                std::get<5>(flattened).insert(std::get<5>(flattened).end(), std::get<5>(edge).begin(),
                                              std::get<5>(edge).end());
            }
            EXPECT_TRUE(field_lengths_match(flattened));
            return flattened;
        }

        static bool field_lengths_match(const edge_fields_t& edge) {
            return std::get<0>(edge).size() == std::get<1>(edge).size() &&
                   std::get<0>(edge).size() == std::get<2>(edge).size() &&
                   std::get<0>(edge).size() == std::get<3>(edge).size() &&
                   std::get<0>(edge).size() == std::get<4>(edge).size() &&
                   std::get<0>(edge).size() == std::get<5>(edge).size();
        }

        std::vector<int64_t> alignment_model_vertex_id_;
        std::vector<std::string> alignment_vertex_label_;
        std::vector<std::string> alignment_move_type_;
        std::vector<int64_t> alignment_activity_index_;
        std::vector<std::string> alignment_deviation_category_;

        std::vector<edge_fields_t> sync_edges_;
        std::vector<edge_fields_t> model_edges_;
        std::vector<edge_fields_t> skip_edges_;
        std::vector<edge_fields_t> log_edges_;
        std::vector<edge_fields_t> unmapped_edges_;
        std::vector<edge_fields_t> missing_violation_edges_;
        std::vector<edge_fields_t> exclusive_violation_edges_;
    };

    class CreateAlignmentTestResultMapBuilder {
    public:
        CreateAlignmentTestResultMapBuilder() = default;
        CreateAlignmentTestResultMapBuilder&& add(const Variant& variant, Result result) {
            result_map_.emplace(variant, std::move(result));
            return std::move(*this);
        }
        ResultMap build() && { return std::move(result_map_); }

    private:
        ResultMap result_map_;
    };

protected:
    CelonisCreateAlignmentTest()
            : arg_types_{{TypeDescriptor::from_logical_type(TYPE_ARRAY),
                          TypeDescriptor::from_logical_type(TYPE_VARCHAR)}},
              return_type_(TypeDescriptor::from_logical_type(TYPE_STRUCT)) {
        // Initialize the struct type descriptor properly
        auto struct_desc = TypeDescriptor::from_logical_type(TYPE_STRUCT);
        std::vector<TypeDescriptor> children = {celonis::array_type(TYPE_BIGINT), celonis::array_type(TYPE_VARCHAR),
                                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_BIGINT),
                                                celonis::array_type(TYPE_VARCHAR)};
        std::vector<std::string> field_names = {"alignment_model_vertex_id", "alignment_vertex_label",
                                                "alignment_move_type", "alignment_activity_index",
                                                "alignment_deviation_category"};
        for (auto type : ::celonis::accelerator::operators::process::align_model::CS_EDGE_TYPES) {
            children.push_back(celonis::array_type(TYPE_BIGINT));
            children.push_back(celonis::array_type(TYPE_VARCHAR));
            children.push_back(celonis::array_type(TYPE_VARCHAR));
            children.push_back(celonis::array_type(TYPE_VARCHAR));
            children.push_back(celonis::array_type(TYPE_BIGINT));
            children.push_back(celonis::array_type(TYPE_BIGINT));

            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "model_vertex_id"));
            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "vertex_label"));
            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "move_type"));
            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "deviation_category"));
            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "edge_class"));
            field_names.push_back(fmt::format("{}_{}", cs_edge_type_to_string_v2(type), "alignment_index"));
        }
        struct_desc.children = std::move(children);
        struct_desc.field_names = std::move(field_names);
        return_type_ = struct_desc;
    }

    void SetUp() override {}

    void TearDown() override {}

private:
    FunctionContext::TypeDesc TYPEDESC_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    FunctionContext::TypeDesc TYPEDESC_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

    class Evaluator {
    public:
        Evaluator(const StructColumn& result, const std::vector<Result>& expected,
                  const FunctionContext::TypeDesc& return_type)
                : result_(result), expected_(expected), return_type_(return_type) {}

        void evaluate() {
            ASSERT_EQ(result_.size(), expected_.size());
            for (int row = 0; row < result_.size(); ++row) {
                compare_array<0, int64_t>(row);
                compare_array<1, std::string>(row);
                compare_array<2, std::string>(row);
                compare_array<3, int64_t>(row);
                compare_array<4, std::string>(row);

                constexpr size_t num_alignment_fields{5};
                constexpr size_t num_edge_fields{6};
                compare_edge_arrays<0 * num_edge_fields + num_alignment_fields>(row); // SYNC edge types
                compare_edge_arrays<1 * num_edge_fields + num_alignment_fields>(row); // MODEL edge types
                compare_edge_arrays<2 * num_edge_fields + num_alignment_fields>(row); // SKIP edge types
                compare_edge_arrays<3 * num_edge_fields + num_alignment_fields>(row); // LOG edge types
                compare_edge_arrays<4 * num_edge_fields + num_alignment_fields>(row); // UNMAPPED edge types
                compare_edge_arrays<5 * num_edge_fields + num_alignment_fields>(row); // MISSING_VIOLATION edge types
                compare_edge_arrays<6 * num_edge_fields + num_alignment_fields>(row); // EXCLUSIVE_VIOLATION edge types
            }
        }

    private:
        template <int offset>
        void compare_edge_arrays(const int row) {
            compare_array<offset + 0, int64_t>(row);
            compare_array<offset + 1, std::string>(row);
            compare_array<offset + 2, std::string>(row);
            compare_array<offset + 3, std::string>(row);
            compare_array<offset + 4, int64_t>(row);
            compare_array<offset + 5, int64_t>(row);
        }

        template <int field, typename TYPE>
        void compare_array(int row) {
            if (result_.fields()[field]->get(row).is_null()) {
                EXPECT_TRUE(std::get<field>(expected_[row]).empty());
                return;
            }
            auto result_array = result_.fields()[field]->get(row).get_array();
            const auto& expected_array = std::get<field>(expected_[row]);
            ASSERT_EQ(result_array.size(), expected_array.size())
                    << "row: " << row << ", field: " << return_type_.field_names[field];
            for (int i = 0; i < result_array.size(); i++) {
                if constexpr (std::is_same_v<TYPE, std::string>) {
                    EXPECT_EQ(result_array[i].get_slice(), expected_array[i])
                            << "row: " << row << ", field: " << return_type_.field_names[field] << ", element: " << i;
                } else if constexpr (std::is_same_v<TYPE, int64_t>) {
                    EXPECT_EQ(result_array[i].get_int64(), expected_array[i])
                            << "row: " << row << ", field: " << return_type_.field_names[field] << ", element: " << i;
                } else {
                    static_assert("Invalid type");
                }
            }
        }

        const StructColumn& result_;
        const std::vector<Result>& expected_;
        const FunctionContext::TypeDesc& return_type_;
    };

    ColumnPtr build_variant_column(const std::vector<std::vector<std::string>>& rows) {
        auto array_column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), /*nullable=*/true);
        for (int i = 0; i < rows.size(); i++) {
            if (rows[i].size() == 0) {
                array_column->append_nulls(1);
                continue;
            }
            DatumArray array;
            for (int j = 0; j < rows[i].size(); j++) {
                if (rows[i][j] == "null") {
                    array.push_back(kNullDatum);
                } else {
                    array.emplace_back(Slice(rows[i][j]));
                }
            }
            array_column->append_datum(array);
        }
        return array_column;
    }

    void ExpectInvalidModelAtPrepare(const std::string& bpmn_model_description_json) {
        auto variants = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), /*nullable=*/true);
        variants->append_nulls(1);
        auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(bpmn_model_description_json, 1);

        Columns columns;
        columns.push_back(variants);
        columns.push_back(model_column);

        auto arg_types = arg_types_;
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type_));
        ctx->set_constant_columns(columns);

        const auto status = CelonisCreateAlignment::create_alignment_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        EXPECT_TRUE(status.is_invalid_argument());
        EXPECT_EQ(nullptr, ctx->get_function_state(FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));

        ASSERT_OK(CelonisCreateAlignment::create_alignment_close(ctx.get(),
                                                                 FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }

    void Run(const VariantRows& variant_rows, const std::string& bpmn_model_description_json,
             const std::vector<Result>& expected) {
        auto variants = build_variant_column(variant_rows);
        auto model_column =
                ColumnHelper::create_const_column<TYPE_VARCHAR>(bpmn_model_description_json, variant_rows.size());

        Columns columns;
        columns.push_back(variants);
        columns.push_back(model_column);

        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
        ctx->set_constant_columns(columns);

        DeferOp close_fragment_local(
                [&ctx] { CelonisCreateAlignment::create_alignment_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL); });
        ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local(
                [&ctx] { CelonisCreateAlignment::create_alignment_close(ctx.get(), FunctionContext::THREAD_LOCAL); });
        ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(ctx.get(),
                                                                   FunctionContext::FunctionStateScope::THREAD_LOCAL));

        const auto result = CelonisCreateAlignment::create_alignment(ctx.get(), columns).value();
        ASSERT_TRUE(result->is_struct());
        const StructColumn* st = down_cast<const StructColumn*>(result.get());
        Evaluator evaluator(*st, expected, return_type_);
        evaluator.evaluate();
    }

    std::vector<FunctionContext::TypeDesc> arg_types_;
    FunctionContext::TypeDesc return_type_;
};

static const CelonisCreateAlignmentTest::ResultMap get_parallel_model_results() {
    auto alignment_ac{
            CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                    .alignment_model_vertex_id({1, 4, 3})
                    .alignment_vertex_label({"A", "C", "B"})
                    .alignment_move_type({"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE"})
                    .alignment_activity_index({0, 1, 0})
                    .alignment_deviation_category({"CONFORMING", "CONFORMING", "MISSING"})
                    .add_sync_edge(
                            {{0, 1, 2, 4, 5, 6},
                             {"BPMN_START", "A", "BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END"},
                             {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                             {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                             {0, 0, 0, 0, 0, 0},
                             {0, 0, 0, 1, 1, 1}})
                    .add_model_edge({{2, 3, 5},
                                     {"BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
                                     {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                     {"CONFORMING", "MISSING", "CONFORMING"},
                                     {0, 0, 0},
                                     {0, 2, 1}})
                    .add_skip_edge({{2, 5},
                                    {"BPMN_PARALLEL", "BPMN_PARALLEL"},
                                    {"GATEWAY_MOVE", "GATEWAY_MOVE"},
                                    {"CONFORMING", "CONFORMING"},
                                    {0, 0},
                                    {0, 1}})
                    .add_missing_violation_edge({{1, 3, 6},
                                                 {"A", "B", "BPMN_END"},
                                                 {"SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                                 {"CONFORMING", "MISSING", "CONFORMING"},
                                                 {0, 0, 0},
                                                 {0, 2, 1}})};

    auto alignment_cbb{CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                               .alignment_model_vertex_id({1, 4, 3, 3})
                               .alignment_vertex_label({"A", "C", "B", "B"})
                               .alignment_move_type({"MODEL_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE"})
                               .alignment_activity_index({0, 0, 1, 2})
                               .alignment_deviation_category({"MISSING", "CONFORMING", "CONFORMING", "EXCESSIVE"})
                               .add_sync_edge({{2, 4, 5, 6},
                                               {"BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END"},
                                               {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                                               {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                                               {0, 0, 0, 0},
                                               {0, 1, 2, 3}})
                               .add_sync_edge({{2, 3, 5},
                                               {"BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
                                               {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE"},
                                               {"CONFORMING", "CONFORMING", "CONFORMING"},
                                               {1, 1, 1},
                                               {0, 2, 2}})
                               .add_model_edge({{0, 1, 2},
                                                {"BPMN_START", "A", "BPMN_PARALLEL"},
                                                {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                                {"CONFORMING", "MISSING", "CONFORMING"},
                                                {0, 0, 0},
                                                {0, 0, 0}})
                               .add_skip_edge({{0, 2},
                                               {"BPMN_START", "BPMN_PARALLEL"},
                                               {"GATEWAY_MOVE", "GATEWAY_MOVE"},
                                               {"CONFORMING", "CONFORMING"},
                                               {0, 0},
                                               {0, 0}})
                               .add_missing_violation_edge({{0, 1, 4},
                                                            {"BPMN_START", "A", "C"},
                                                            {"GATEWAY_MOVE", "MODEL_MOVE", "SYNC_MOVE"},
                                                            {"CONFORMING", "MISSING", "CONFORMING"},
                                                            {0, 0, 0},
                                                            {0, 0, 1}})
                               .add_log_edge({{3, 3, 6},
                                              {"B", "B", "BPMN_END"},
                                              {"SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE"},
                                              {"CONFORMING", "EXCESSIVE", "CONFORMING"},
                                              {0, 0, 0},
                                              {2, 3, 3}})};

    return CelonisCreateAlignmentTest::CreateAlignmentTestResultMapBuilder{}
            .add({"A", "C"}, alignment_ac.build())
            .add({"A", "B", "C"},
                 CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                         .alignment_model_vertex_id({1, 3, 4})
                         .alignment_vertex_label({"A", "B", "C"})
                         .alignment_move_type({"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE"})
                         .alignment_activity_index({0, 1, 2})
                         .alignment_deviation_category({"CONFORMING", "CONFORMING", "CONFORMING"})
                         .add_sync_edge(
                                 {{0, 1, 2, 3, 5, 6},
                                  {"BPMN_START", "A", "BPMN_PARALLEL", "B", "BPMN_PARALLEL", "BPMN_END"},
                                  {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                                   "GATEWAY_MOVE"},
                                  {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                                  {0, 0, 0, 0, 0, 0},
                                  {0, 0, 0, 1, 2, 2}})
                         .add_sync_edge({{2, 4, 5},
                                         {
                                                 "BPMN_PARALLEL",
                                                 "C",
                                                 "BPMN_PARALLEL",
                                         },
                                         {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING", "CONFORMING"},
                                         {1, 1, 1},
                                         {0, 2, 2}})
                         .build())
            .add({"C", "B", "B"}, alignment_cbb.build())
            //In the presence of nulls only the alignment<->activity join changes
            .add({"C", "B", "null", "B"}, alignment_cbb.alignment_activity_index({0, 0, 1, 3}).build())
            .add({{"A", "null", "null", "C"}}, alignment_ac.alignment_activity_index({0, 3, 0}).build())
            .add({"null", "A", "null", "C", "null"}, alignment_ac.alignment_activity_index({1, 3, 1}).build())
            .build();
};

static const CelonisCreateAlignmentTest::ResultMap get_loop_model_results() {
    return CelonisCreateAlignmentTest::CreateAlignmentTestResultMapBuilder{}
            .add({"A", "B", "C", "A", "B"},
                 CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                         .alignment_model_vertex_id({2, 3, 5, 2, 3})
                         .alignment_vertex_label({"A", "B", "C", "A", "B"})
                         .alignment_move_type({"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE"})
                         .alignment_activity_index({0, 1, 2, 3, 4})
                         .alignment_deviation_category(
                                 {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"})
                         .add_sync_edge(
                                 {{0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
                                  {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C",
                                   "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                                  {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                                   "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                                   "GATEWAY_MOVE"},
                                  {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
                                   "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                                  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 4}})
                         .build())
            .add({"A", "B", "C"},
                 CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                         .alignment_model_vertex_id({2, 3, 5, 2, 3})
                         .alignment_vertex_label({"A", "B", "C", "A", "B"})
                         .alignment_move_type({"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE", "MODEL_MOVE"})
                         .alignment_activity_index({0, 1, 2, 2, 2})
                         .alignment_deviation_category({"CONFORMING", "CONFORMING", "CONFORMING", "MISSING", "MISSING"})
                         .add_sync_edge({{0, 1, 2, 3, 4, 5, 1},
                                         {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C",
                                          "BPMN_EXCLUSIVE_CHOICE"},
                                         {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                                          "SYNC_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
                                          "CONFORMING", "CONFORMING"},
                                         {0, 0, 0, 0, 0, 0, 0},
                                         {0, 0, 0, 1, 1, 2, 2}})
                         .add_sync_edge({{4, 6},
                                         {"BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                                         {"GATEWAY_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING"},
                                         {1, 1},
                                         {4, 4}})
                         .add_model_edge({{1, 2, 3, 4},
                                          {"BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE"},
                                          {"GATEWAY_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                          {"CONFORMING", "MISSING", "MISSING", "CONFORMING"},
                                          {0, 0, 0, 0},
                                          {2, 3, 4, 4}})
                         .add_skip_edge({{1, 4},
                                         {"BPMN_EXCLUSIVE_CHOICE", "BPMN_EXCLUSIVE_CHOICE"},
                                         {"GATEWAY_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING"},
                                         {0, 0},
                                         {2, 4}})
                         .add_missing_violation_edge({{5, 2, 3, 6},
                                                      {"C", "A", "B", "BPMN_END"},
                                                      {"SYNC_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                                      {"CONFORMING", "MISSING", "MISSING", "CONFORMING"},
                                                      {0, 0, 0, 0},
                                                      {2, 3, 4, 4}})
                         .build())
            .add({"A", "B", "A", "B"},
                 CelonisCreateAlignmentTest::CreateAlignmentTestResultBuilder{}
                         .alignment_model_vertex_id({2, 3, 5, 2, 3})
                         .alignment_vertex_label({"A", "B", "C", "A", "B"})
                         .alignment_move_type({"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE", "SYNC_MOVE", "SYNC_MOVE"})
                         .alignment_activity_index({0, 1, 1, 2, 3})
                         .alignment_deviation_category(
                                 {"CONFORMING", "CONFORMING", "MISSING", "CONFORMING", "CONFORMING"})
                         .add_sync_edge({{0, 1, 2, 3, 4},
                                         {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE"},
                                         {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                                         {0, 0, 0, 0, 0},
                                         {0, 0, 0, 1, 1}})
                         .add_sync_edge({{1, 2, 3, 4, 6},
                                         {"BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                                         {"GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
                                         {1, 1, 1, 1, 1},
                                         {2, 3, 4, 4, 4}})
                         .add_model_edge({{4, 5, 1},
                                          {"BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE"},
                                          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
                                          {"CONFORMING", "MISSING", "CONFORMING"},
                                          {0, 0, 0},
                                          {1, 2, 2}})
                         .add_skip_edge({{4, 1},
                                         {"BPMN_EXCLUSIVE_CHOICE", "BPMN_EXCLUSIVE_CHOICE"},
                                         {"GATEWAY_MOVE", "GATEWAY_MOVE"},
                                         {"CONFORMING", "CONFORMING"},
                                         {0, 0},
                                         {1, 2}})
                         .add_missing_violation_edge({{3, 5, 2},
                                                      {"B", "C", "A"},
                                                      {"SYNC_MOVE", "MODEL_MOVE", "SYNC_MOVE"},
                                                      {"CONFORMING", "MISSING", "CONFORMING"},
                                                      {0, 0, 0},
                                                      {1, 2, 3}})
                         .build())
            .build();
}

TEST_F(CelonisCreateAlignmentTest, Parallel) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"C", "B", "null", "B"}};
    auto parallel_model_results{get_parallel_model_results()};
    std::vector<Result> expected = {parallel_model_results.at(variants[0]), parallel_model_results.at(variants[1]),
                                    parallel_model_results.at(variants[2])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisCreateAlignmentTest, Loop) {
    VariantRows variants = {{"A", "B", "C", "A", "B"}, {"A", "B", "C"}, {"A", "B", "A", "B"}};
    auto loop_model_results{get_loop_model_results()};
    std::vector<Result> expected = {loop_model_results.at(variants[0]), loop_model_results.at(variants[1]),
                                    loop_model_results.at(variants[2])};
    Run(variants, LOOP_MODEL, expected);
}

TEST_F(CelonisCreateAlignmentTest, Parallel_DuplicatedVariants) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"A", "C"}, {"C", "B", "null", "B"}};
    auto parallel_model_results{get_parallel_model_results()};
    std::vector<Result> expected = {parallel_model_results.at(variants[0]), parallel_model_results.at(variants[1]),
                                    parallel_model_results.at(variants[2]), parallel_model_results.at(variants[3])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisCreateAlignmentTest, Parallel_NULL) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {},
                            {"null", "null"},
                            {"C", "B", "B"},
                            {"C", "B", "null", "B"},
                            {"A", "null", "null", "C"},
                            {"null", "A", "null", "C", "null"},
                            {"A", "null", "null", "C"},
                            {}};
    auto parallel_model_results{get_parallel_model_results()};
    std::vector<Result> expected = {parallel_model_results.at(variants[0]),
                                    parallel_model_results.at(variants[1]),
                                    {},
                                    {},
                                    parallel_model_results.at(variants[4]),
                                    parallel_model_results.at(variants[5]),
                                    parallel_model_results.at(variants[6]),
                                    parallel_model_results.at(variants[7]),
                                    parallel_model_results.at(variants[8]),
                                    {}};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisCreateAlignmentTest, Parallel_ONLYNULL) {
    VariantRows variants = {{}, {}};
    std::vector<Result> expected = {{}, {}};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisCreateAlignmentTest, InvalidModel) {
    ExpectInvalidModelAtPrepare("Invalid JSON");
}

TEST_F(CelonisCreateAlignmentTest, InvalidTaskName) {
    ExpectInvalidModelAtPrepare(R"json({"nodes":[{"node_id":"1","node_type":1}],"edges":[],"cache_key":"model"})json");
    ExpectInvalidModelAtPrepare(
            R"json({"nodes":[{"node_id":"1","node_type":4,"task_name":"A"}],"edges":[],"cache_key":"model"})json");
}

TEST_F(CelonisCreateAlignmentTest, LongVariant) {
    // Test that variants longer than 40000 activities are now supported
    // Previously this would throw an InternalError
    std::vector<std::string> long_variant;
    long_variant.reserve(40005);

    // Create a variant with 40005 activities
    for (int i = 0; i < 40005; i++) {
        long_variant.emplace_back("A");
    }

    VariantRows variants = {long_variant};

    auto array_column = build_variant_column(variants);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, 1);

    Columns columns;
    columns.push_back(array_column);
    columns.push_back(model_column);

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(ctx.get(),
                                                               FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(ctx.get(),
                                                               FunctionContext::FunctionStateScope::THREAD_LOCAL));

    // This should not throw an InternalError about variants longer than 40000
    const auto result = CelonisCreateAlignment::create_alignment(ctx.get(), columns);
    ASSERT_TRUE(result.ok()) << "Expected variants longer than 40000 to be supported after CPML fix, but got error: "
                             << result.status().message();

    ASSERT_OK(CelonisCreateAlignment::create_alignment_close(ctx.get(),
                                                             FunctionContext::FunctionStateScope::THREAD_LOCAL));
    ASSERT_OK(CelonisCreateAlignment::create_alignment_close(ctx.get(),
                                                             FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
}

TEST_F(CelonisCreateAlignmentTest, SharedFragmentStateConcurrency) {
    VariantRows variants = {{"A", "C"}};
    const auto parallel_model_results = get_parallel_model_results();
    const std::vector<Result> expected = {parallel_model_results.at(variants[0])};

    Columns columns;
    columns.push_back(build_variant_column(variants));
    columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, variants.size()));

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    DeferOp close_fragment_local(
            [&ctx] { CelonisCreateAlignment::create_alignment_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL); });
    ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(ctx.get(),
                                                               FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));

    constexpr int num_threads = 16;
    std::vector<std::unique_ptr<FunctionContext>> thread_contexts;
    thread_contexts.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        thread_contexts.emplace_back(ctx->clone(nullptr));
        EXPECT_EQ(ctx->get_function_state(FunctionContext::FRAGMENT_LOCAL),
                  thread_contexts.back()->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    }

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (const auto& thread_context : thread_contexts) {
        threads.emplace_back([&, context = thread_context.get()] {
            ASSERT_OK(CelonisCreateAlignment::create_alignment_prepare(context, FunctionContext::THREAD_LOCAL));
            DeferOp close_thread_local([context] {
                CelonisCreateAlignment::create_alignment_close(context, FunctionContext::THREAD_LOCAL);
            });

            const auto result = CelonisCreateAlignment::create_alignment(context, columns);
            ASSERT_TRUE(result.ok()) << result.status().message();
            ASSERT_TRUE(result.value()->is_struct());
            const auto* st = down_cast<const StructColumn*>(result.value().get());
            Evaluator evaluator(*st, expected, return_type_);
            evaluator.evaluate();
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_F(CelonisCreateAlignmentTest, Concurrency) {
    int num_inputs = 100;
    int num_threads = 1000;

    const std::vector<std::string> models{PARALLEL_MODEL, LOOP_MODEL};
    const auto parallel_model_results = get_parallel_model_results();
    const auto loop_model_results = get_loop_model_results();
    std::vector<ResultMap::const_iterator> results[2];
    for (auto it = parallel_model_results.cbegin(); it != parallel_model_results.cend(); it++) {
        results[0].push_back(it);
    }
    for (auto it = loop_model_results.cbegin(); it != loop_model_results.cend(); it++) {
        results[1].push_back(it);
    }

    struct Input {
        Input(const std::string& model, VariantRows variants, std::vector<Result> expected)
                : model(model), variants(std::move(variants)), expected(std::move(expected)) {}

        const std::string& model;
        VariantRows variants;
        std::vector<Result> expected;
    };

    std::vector<Input> inputs;
    inputs.reserve(num_threads);

    std::random_device rd;
    std::uniform_int_distribution<size_t> model_d(0, 1);
    std::uniform_int_distribution<size_t> num_variants_d(1, 20);

    for (int i = 0; i < num_inputs; i++) {
        int model = model_d(rd);
        VariantRows variants;
        std::vector<Result> expected;
        int num_variants = num_variants_d(rd);
        std::uniform_int_distribution<size_t> variant_d(0, results[model].size() - 1);
        for (int j = 0; j < num_variants; j++) {
            int variant = variant_d(rd);
            variants.push_back(results[model][variant]->first);
            expected.push_back(results[model][variant]->second);
        }
        inputs.emplace_back(models[model], std::move(variants), std::move(expected));
    }

    std::uniform_int_distribution<size_t> input_d(0, num_inputs - 1);
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        int id = input_d(rd);
        threads.emplace_back([&](const Input& input) { Run(input.variants, input.model, input.expected); }, inputs[id]);
    }
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace starrocks
