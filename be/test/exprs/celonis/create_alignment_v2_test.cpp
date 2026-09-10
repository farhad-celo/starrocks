#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bpmn_models.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/celonis/create_alignment.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/celonis/modules/operators/process/align_model/v2/create_alignment_output_projection.h"
#include "testutil/assert.h"
#include "util.h"
#include "util/defer_op.h"
#include "util/slice.h"

namespace starrocks {
namespace {

using ::celonis::accelerator::operators::process::align_model::AlignModelHelper;
using ::celonis::accelerator::operators::process::align_model::v2::association_output_column;
using ::celonis::accelerator::operators::process::align_model::v2::create_alignment_output_projection;
using ::celonis::accelerator::operators::process::align_model::v2::create_alignment_output_value_type;

// With trace B, the gateways separate the missing A and incomplete C into distinct L1_MISSING components.
constexpr const char* GATEWAY_SEPARATED_MODEL = R"json({
    "nodes": [
        {"node_id": "0", "node_type": 4},
        {"node_id": "1", "node_type": 1, "task_name": "A"},
        {"node_id": "2", "node_type": 2},
        {"node_id": "3", "node_type": 1, "task_name": "B"},
        {"node_id": "4", "node_type": 1, "task_name": "D"},
        {"node_id": "5", "node_type": 2},
        {"node_id": "6", "node_type": 1, "task_name": "C"},
        {"node_id": "7", "node_type": 5}
    ],
    "edges": [
        {"from": "0", "to": "1"},
        {"from": "1", "to": "2"},
        {"from": "2", "to": "3"},
        {"from": "2", "to": "4"},
        {"from": "3", "to": "5"},
        {"from": "4", "to": "5"},
        {"from": "5", "to": "6"},
        {"from": "6", "to": "7"}
    ],
    "cache_key": "GATEWAY_SEPARATED_MODEL"
})json";

class CelonisCreateAlignmentV2Test : public testing::Test {
protected:
    using VariantRows = std::vector<std::vector<std::string>>;

    static FunctionContext::TypeDesc make_return_type(std::uint64_t mask) {
        auto type = TypeDescriptor::from_logical_type(TYPE_STRUCT);
        for (size_t bit = 0; bit < create_alignment_output_projection::fields().size(); ++bit) {
            if ((mask & (std::uint64_t{1} << bit)) == 0) {
                continue;
            }
            const auto& field = create_alignment_output_projection::fields()[bit];
            type.field_names.emplace_back(field.name);
            switch (field.value_type) {
            case create_alignment_output_value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY:
            case create_alignment_output_value_type::ROW_ID_ARRAY:
                type.children.emplace_back(celonis::array_type(TYPE_BIGINT));
                break;
            case create_alignment_output_value_type::STRING_ARRAY:
                type.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
                break;
            }
        }
        return type;
    }

    static ColumnPtr make_variant_column(const VariantRows& rows) {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        for (const auto& row : rows) {
            if (row.empty()) {
                column->append_nulls(1);
                continue;
            }
            DatumArray values;
            for (const auto& activity : row) {
                if (activity == "null") {
                    values.emplace_back(kNullDatum);
                } else {
                    values.emplace_back(Slice(activity));
                }
            }
            column->append_datum(values);
        }
        return column;
    }

    static StatusOr<ColumnPtr> execute_with_variant_column(bool use_v2, ColumnPtr variant_column, size_t row_count,
                                                           std::optional<std::int64_t> mask,
                                                           const FunctionContext::TypeDesc& return_type,
                                                           const char* model_json) {
        Columns columns{std::move(variant_column),
                        ColumnHelper::create_const_column<TYPE_VARCHAR>(model_json, row_count)};
        std::vector<FunctionContext::TypeDesc> argument_types{celonis::array_type(TYPE_VARCHAR),
                                                              TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
        if (use_v2 && mask.has_value()) {
            columns.emplace_back(ColumnHelper::create_const_column<TYPE_BIGINT>(mask.value(), row_count));
            argument_types.emplace_back(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        }

        std::unique_ptr<FunctionContext> context(
                FunctionContext::create_test_context(std::move(argument_types), return_type));
        context->set_constant_columns(columns);
        const auto prepare = use_v2 ? CelonisCreateAlignment::create_alignment_v2_prepare
                                    : CelonisCreateAlignment::create_alignment_prepare;
        RETURN_IF_ERROR(prepare(context.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_fragment([&context] {
            CelonisCreateAlignment::create_alignment_close(context.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(prepare(context.get(), FunctionContext::THREAD_LOCAL));
        DeferOp close_thread([&context] {
            CelonisCreateAlignment::create_alignment_close(context.get(), FunctionContext::THREAD_LOCAL);
        });
        return use_v2 ? CelonisCreateAlignment::create_alignment_v2(context.get(), columns)
                      : CelonisCreateAlignment::create_alignment(context.get(), columns);
    }

    static StatusOr<ColumnPtr> execute(bool use_v2, const VariantRows& variants, std::int64_t mask,
                                       const FunctionContext::TypeDesc& return_type,
                                       const char* model_json = PARALLEL_MODEL) {
        return execute_with_variant_column(use_v2, make_variant_column(variants), variants.size(),
                                           std::optional<std::int64_t>{mask}, return_type, model_json);
    }

    static StatusOr<ColumnPtr> execute_v2_all_fields(const VariantRows& variants,
                                                     const char* model_json = PARALLEL_MODEL) {
        return execute_with_variant_column(true, make_variant_column(variants), variants.size(), std::nullopt,
                                           make_return_type(create_alignment_output_projection::ALL_FIELDS_MASK),
                                           model_json);
    }

    static void expect_results_match(std::uint64_t mask, size_t row_count, const StatusOr<ColumnPtr>& legacy_result,
                                     const StatusOr<ColumnPtr>& v2_result) {
        ASSERT_OK(legacy_result.status());
        ASSERT_OK(v2_result.status());

        const auto* legacy = down_cast<const StructColumn*>(legacy_result.value().get());
        const auto* v2 = down_cast<const StructColumn*>(v2_result.value().get());
        ASSERT_EQ(std::popcount(mask), v2->fields().size());
        ASSERT_EQ(v2->fields().size(), v2->field_names().size());

        for (size_t v2_field = 0; v2_field < v2->field_names().size(); ++v2_field) {
            const auto& field_name = v2->field_names()[v2_field];
            const auto legacy_iter = std::ranges::find(legacy->field_names(), field_name);
            ASSERT_NE(legacy->field_names().end(), legacy_iter);
            const auto legacy_field = std::distance(legacy->field_names().begin(), legacy_iter);
            for (size_t row = 0; row < row_count; ++row) {
                const auto& v2_column{*v2->fields()[v2_field]};
                const auto& v1_column{*legacy->fields()[legacy_field]};
                EXPECT_TRUE(v2_column.equals(row, v1_column, row) ||
                            (std::regex_replace(v2_column.debug_item(row), std::regex{"INCOMPLETE"}, "MISSING") ==
                             v1_column.debug_item(row)))
                        << "field=" << field_name << " row=" << row << " v2=" << v2_column.debug_item(row)
                        << " legacy=" << v1_column.debug_item(row);
            }
        }
    }

    static void expect_v2_matches_legacy(std::uint64_t mask, const VariantRows& variants,
                                         const char* model_json = PARALLEL_MODEL) {
        const auto legacy_result =
                execute(false, variants, create_alignment_output_projection::ALL_FIELDS_V1_MASK,
                        make_return_type(create_alignment_output_projection::ALL_FIELDS_V1_MASK), model_json);
        const auto v2_result = execute(true, variants, mask, make_return_type(mask), model_json);
        expect_results_match(mask, variants.size(), legacy_result, v2_result);
    }

    static void expect_v2_matches_legacy(std::uint64_t mask) {
        expect_v2_matches_legacy(mask, VariantRows{{"A", "C"}, {"A", "B", "C"}, {}, {"C", "B", "null", "B"}});
    }
};

TEST_F(CelonisCreateAlignmentV2Test, AllV1FieldsMatchLegacy) {
    expect_v2_matches_legacy(create_alignment_output_projection::ALL_FIELDS_V1_MASK);
}

TEST_F(CelonisCreateAlignmentV2Test, LegacyKeepsMissingDeviationSemantics) {
    const auto result = execute(false, {{"A", "C"}}, create_alignment_output_projection::ALL_FIELDS_V1_MASK,
                                make_return_type(create_alignment_output_projection::ALL_FIELDS_V1_MASK));
    ASSERT_OK(result.status());

    const auto* output = down_cast<const StructColumn*>(result.value().get());
    ASSERT_EQ(create_alignment_output_projection::V1_FIELD_COUNT, output->fields().size());
    const auto& deviation_categories = output->fields()[4]->debug_item(0);
    EXPECT_NE(std::string::npos, deviation_categories.find("MISSING"));
    EXPECT_EQ(std::string::npos, deviation_categories.find("INCOMPLETE"));
}

TEST_F(CelonisCreateAlignmentV2Test, TwoArgumentV2MatchesAllFieldsMask) {
    const VariantRows variants{{"A", "C"}, {"A", "B", "C"}};
    const auto all_fields_result = execute_v2_all_fields(variants);
    const auto masked_result = execute(true, variants, create_alignment_output_projection::ALL_FIELDS_MASK,
                                       make_return_type(create_alignment_output_projection::ALL_FIELDS_MASK));
    ASSERT_OK(all_fields_result.status());
    ASSERT_OK(masked_result.status());

    const auto* all_fields = down_cast<const StructColumn*>(all_fields_result.value().get());
    const auto* masked = down_cast<const StructColumn*>(masked_result.value().get());
    ASSERT_EQ(create_alignment_output_projection::FIELD_COUNT, all_fields->fields().size());
    ASSERT_EQ(masked->field_names(), all_fields->field_names());
    for (size_t field = 0; field < all_fields->fields().size(); ++field) {
        for (size_t row = 0; row < variants.size(); ++row) {
            EXPECT_TRUE(all_fields->fields()[field]->equals(row, *masked->fields()[field], row))
                    << "field=" << all_fields->field_names()[field] << " row=" << row;
        }
    }
}

TEST_F(CelonisCreateAlignmentV2Test, IncompleteOnlyProjectionIsPopulated) {
    constexpr size_t incomplete_deviation_category_bit{
            create_alignment_output_projection::V1_FIELD_COUNT +
            static_cast<size_t>(association_output_column::DEVIATION_CATEGORY)};
    constexpr std::uint64_t mask{std::uint64_t{1} << incomplete_deviation_category_bit};
    const auto result = execute(true, {{"A", "C"}}, mask, make_return_type(mask));
    ASSERT_OK(result.status());

    const auto* output = down_cast<const StructColumn*>(result.value().get());
    ASSERT_EQ(1, output->fields().size());
    EXPECT_EQ("INCOMPLETE_VIOLATION_deviation_category", output->field_names()[0]);
    EXPECT_NE(std::string::npos, output->fields()[0]->debug_item(0).find("INCOMPLETE"));
}

TEST_F(CelonisCreateAlignmentV2Test, IndividualIncompleteFieldsUseIndependentProjectionAndEdgeClasses) {
    const VariantRows variants{{"B"}};
    const auto all_fields_result =
            execute(true, variants, create_alignment_output_projection::ALL_FIELDS_MASK,
                    make_return_type(create_alignment_output_projection::ALL_FIELDS_MASK), GATEWAY_SEPARATED_MODEL);
    ASSERT_OK(all_fields_result.status());

    const auto* all_fields = down_cast<const StructColumn*>(all_fields_result.value().get());
    ASSERT_EQ(create_alignment_output_projection::FIELD_COUNT, all_fields->fields().size());
    for (size_t bit = create_alignment_output_projection::V1_FIELD_COUNT;
         bit < create_alignment_output_projection::FIELD_COUNT; ++bit) {
        const auto mask = std::uint64_t{1} << bit;
        const auto projected_result = execute(true, variants, mask, make_return_type(mask), GATEWAY_SEPARATED_MODEL);
        ASSERT_OK(projected_result.status());

        const auto* projected = down_cast<const StructColumn*>(projected_result.value().get());
        ASSERT_EQ(1, projected->fields().size());
        ASSERT_EQ(1, projected->field_names().size());
        EXPECT_EQ(std::string{create_alignment_output_projection::fields()[bit].name}, projected->field_names()[0]);
        EXPECT_TRUE(projected->fields()[0]->equals(0, *all_fields->fields()[bit], 0)) << "bit=" << bit;
        EXPECT_NE("[]", projected->fields()[0]->debug_item(0)) << "bit=" << bit;
    }

    constexpr size_t incomplete_edge_class_bit{create_alignment_output_projection::V1_FIELD_COUNT +
                                               static_cast<size_t>(association_output_column::EDGE_CLASS)};
    const auto incomplete_edge_classes = all_fields->fields()[incomplete_edge_class_bit]->debug_item(0);
    EXPECT_NE("[]", incomplete_edge_classes);
    EXPECT_EQ(std::string::npos, incomplete_edge_classes.find_first_not_of("[0,]"));

    const auto missing_edge_class =
            std::ranges::find(all_fields->field_names(), std::string{"MISSING_VIOLATION_edge_class"});
    ASSERT_NE(all_fields->field_names().end(), missing_edge_class);
    const auto missing_edge_class_bit = std::distance(all_fields->field_names().begin(), missing_edge_class);
    EXPECT_NE(std::string::npos, all_fields->fields()[missing_edge_class_bit]->debug_item(0).find('1'));
}

TEST_F(CelonisCreateAlignmentV2Test, RepresentativeMasksMatchLegacy) {
    expect_v2_matches_legacy(28);
    expect_v2_matches_legacy(1576);
    expect_v2_matches_legacy(((std::uint64_t{1} << 6) - 1) << 5);
    expect_v2_matches_legacy((std::uint64_t{1} << 46) | 1);
    expect_v2_matches_legacy((std::uint64_t{1} << 0) | (std::uint64_t{1} << 11) | (std::uint64_t{1} << 17) |
                             (std::uint64_t{1} << 23) | (std::uint64_t{1} << 29) | (std::uint64_t{1} << 35) |
                             (std::uint64_t{1} << 41));
}

TEST_F(CelonisCreateAlignmentV2Test, IndividualAlignmentFieldsMatchLegacy) {
    for (size_t bit = 0; bit < 5; ++bit) {
        expect_v2_matches_legacy(std::uint64_t{1} << bit);
    }
}

TEST_F(CelonisCreateAlignmentV2Test, LoopModelMatchesLegacy) {
    expect_v2_matches_legacy(28, {{"A", "B", "C", "A", "B"}, {"A", "B", "C"}, {"A", "B", "A", "B"}}, LOOP_MODEL);
}

TEST_F(CelonisCreateAlignmentV2Test, EmptyActivityArrayMatchesLegacy) {
    auto empty_array = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
    empty_array->append_datum(DatumArray{});
    const auto legacy_result = execute_with_variant_column(
            false, empty_array, 1, create_alignment_output_projection::ALL_FIELDS_V1_MASK,
            make_return_type(create_alignment_output_projection::ALL_FIELDS_V1_MASK), PARALLEL_MODEL);
    const auto v2_result =
            execute_with_variant_column(true, std::move(empty_array), 1, 28, make_return_type(28), PARALLEL_MODEL);
    expect_results_match(28, 1, legacy_result, v2_result);
}

TEST_F(CelonisCreateAlignmentV2Test, LongVariantIsSupported) {
    VariantRows variants{VariantRows::value_type(40005, "A")};
    const auto result = execute(true, variants, 4, make_return_type(4));
    ASSERT_OK(result.status());
    ASSERT_EQ(1, result.value()->size());
}

TEST_F(CelonisCreateAlignmentV2Test, ProjectedResultTableContainsOnlyRequestedFields) {
    constexpr std::int64_t mask{28};
    const auto return_type = make_return_type(mask);
    auto projection_or = create_alignment_output_projection::from_mask_and_return_fields(mask, return_type.field_names);
    ASSERT_OK(projection_or.status());
    auto projection = std::move(projection_or).value();
    ASSERT_FALSE(projection.includes_internal_variant());
    ASSERT_FALSE(projection.needs_vertex_labels());
    ASSERT_TRUE(projection.needs_deviation_categories());

    auto model_or = AlignModelHelper::parse_bpmn_model_description(PARALLEL_MODEL);
    ASSERT_OK(model_or.status());
    auto model = std::move(model_or).value();
    AlignModelHelper helper;
    AlignModelHelper::traces_t traces{{std::string{"A"}, std::string{"C"}}};
    ASSERT_OK(helper.execute(traces, model, AlignModelHelper::celostar_align_model_version::V2, projection));

    const auto& columns = helper.result_table().columns();
    ASSERT_EQ(3, columns.size());
    EXPECT_TRUE(columns.contains("alignment_move_type"));
    EXPECT_TRUE(columns.contains("alignment_activity_index"));
    EXPECT_TRUE(columns.contains("alignment_deviation_category"));
    EXPECT_FALSE(columns.contains("variant"));
    EXPECT_FALSE(columns.contains("alignment_vertex_label"));
    EXPECT_FALSE(columns.contains("SYNC_EDGE_model_vertex_id"));

    auto move_type_only_or =
            create_alignment_output_projection::from_mask_and_return_fields(4, make_return_type(4).field_names);
    ASSERT_OK(move_type_only_or.status());
    const auto move_type_only = std::move(move_type_only_or).value();
    EXPECT_FALSE(move_type_only.needs_vertex_labels());
    EXPECT_FALSE(move_type_only.needs_deviation_categories());
}

TEST_F(CelonisCreateAlignmentV2Test, HelperDefaultProjectionMatchesVersion) {
    AlignModelHelper::traces_t traces{{std::string{"A"}, std::string{"C"}}};
    AlignModelHelper v2_helper;
    ASSERT_OK(v2_helper.execute(traces, PARALLEL_MODEL, AlignModelHelper::celostar_align_model_version::V2));

    const auto& v2_columns = v2_helper.result_table().columns();
    EXPECT_EQ(create_alignment_output_projection::V1_FIELD_COUNT + 1, v2_columns.size());
    EXPECT_TRUE(v2_columns.contains("variant"));
    for (size_t bit = 0; bit < create_alignment_output_projection::V1_FIELD_COUNT; ++bit) {
        const auto field_name = std::string{create_alignment_output_projection::fields()[bit].name};
        EXPECT_TRUE(v2_columns.contains(field_name)) << field_name;
    }
    for (size_t bit = create_alignment_output_projection::V1_FIELD_COUNT;
         bit < create_alignment_output_projection::FIELD_COUNT; ++bit) {
        const auto field_name = std::string{create_alignment_output_projection::fields()[bit].name};
        EXPECT_FALSE(v2_columns.contains(field_name)) << field_name;
    }

    AlignModelHelper v3_helper;
    ASSERT_OK(v3_helper.execute(traces, PARALLEL_MODEL, AlignModelHelper::celostar_align_model_version::V3));

    const auto& v3_columns = v3_helper.result_table().columns();
    EXPECT_EQ(create_alignment_output_projection::FIELD_COUNT + 1, v3_columns.size());
    EXPECT_TRUE(v3_columns.contains("variant"));
    for (const auto& field : create_alignment_output_projection::fields()) {
        EXPECT_TRUE(v3_columns.contains(std::string{field.name})) << field.name;
    }
}

TEST_F(CelonisCreateAlignmentV2Test, MaskAndReturnSchemaValidation) {
    const auto one_field_type = make_return_type(1);
    EXPECT_TRUE(create_alignment_output_projection::from_mask_and_return_fields(0, {}).status().is_invalid_argument());
    EXPECT_TRUE(create_alignment_output_projection::from_mask_and_return_fields(-1, {}).status().is_invalid_argument());
    EXPECT_TRUE(create_alignment_output_projection::from_mask_and_return_fields(
                        std::int64_t{1} << create_alignment_output_projection::FIELD_COUNT, {})
                        .status()
                        .is_invalid_argument());
    EXPECT_TRUE(create_alignment_output_projection::from_mask_and_return_fields(1, {}).status().is_internal_error());
    EXPECT_TRUE(create_alignment_output_projection::from_mask_and_return_fields(1, {"alignment_vertex_label"})
                        .status()
                        .is_internal_error());
    ASSERT_OK(create_alignment_output_projection::from_mask_and_return_fields(1, one_field_type.field_names).status());
}

TEST_F(CelonisCreateAlignmentV2Test, PrepareRejectsInvalidMaskColumns) {
    const VariantRows variants{{"A", "C"}};
    auto variant_column = make_variant_column(variants);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, 1);
    const std::vector<FunctionContext::TypeDesc> argument_types{celonis::array_type(TYPE_VARCHAR),
                                                                TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                                TypeDescriptor::from_logical_type(TYPE_BIGINT)};

    auto prepare = [&](ColumnPtr mask_column, bool register_as_constant) {
        std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context(
                std::vector<FunctionContext::TypeDesc>(argument_types), make_return_type(1)));
        context->set_constant_columns(
                {variant_column, model_column, register_as_constant ? std::move(mask_column) : nullptr});
        const auto status =
                CelonisCreateAlignment::create_alignment_v2_prepare(context.get(), FunctionContext::FRAGMENT_LOCAL);
        CelonisCreateAlignment::create_alignment_close(context.get(), FunctionContext::FRAGMENT_LOCAL);
        return status;
    };

    EXPECT_TRUE(prepare(ColumnHelper::create_const_column<TYPE_BIGINT>(0, 1), true).is_invalid_argument());
    EXPECT_TRUE(prepare(ColumnHelper::create_const_column<TYPE_BIGINT>(-1, 1), true).is_invalid_argument());
    EXPECT_TRUE(prepare(ColumnHelper::create_const_column<TYPE_BIGINT>(
                                std::int64_t{1} << create_alignment_output_projection::FIELD_COUNT, 1),
                        true)
                        .is_invalid_argument());
    EXPECT_TRUE(prepare(ColumnHelper::create_const_null_column(1), true).is_invalid_argument());
    EXPECT_TRUE(prepare(ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), false), false)
                        .is_invalid_argument());

    auto wrong_argument_types = argument_types;
    wrong_argument_types[2] = TypeDescriptor::from_logical_type(TYPE_INT);
    std::unique_ptr<FunctionContext> wrong_type_context(
            FunctionContext::create_test_context(std::move(wrong_argument_types), make_return_type(1)));
    wrong_type_context->set_constant_columns(
            {variant_column, model_column, ColumnHelper::create_const_column<TYPE_INT>(1, 1)});
    EXPECT_TRUE(CelonisCreateAlignment::create_alignment_v2_prepare(wrong_type_context.get(),
                                                                    FunctionContext::FRAGMENT_LOCAL)
                        .is_invalid_argument());
}

TEST_F(CelonisCreateAlignmentV2Test, SharedFragmentProjectionIsThreadSafe) {
    constexpr std::int64_t mask{28};
    const VariantRows variants{{"A", "C"}};
    Columns columns{make_variant_column(variants), ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, 1),
                    ColumnHelper::create_const_column<TYPE_BIGINT>(mask, 1)};
    std::vector<FunctionContext::TypeDesc> argument_types{celonis::array_type(TYPE_VARCHAR),
                                                          TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                          TypeDescriptor::from_logical_type(TYPE_BIGINT)};
    std::unique_ptr<FunctionContext> context(
            FunctionContext::create_test_context(std::move(argument_types), make_return_type(mask)));
    context->set_constant_columns(columns);
    ASSERT_OK(CelonisCreateAlignment::create_alignment_v2_prepare(context.get(), FunctionContext::FRAGMENT_LOCAL));
    DeferOp close_fragment([&context] {
        CelonisCreateAlignment::create_alignment_close(context.get(), FunctionContext::FRAGMENT_LOCAL);
    });

    auto expected_or = CelonisCreateAlignment::create_alignment_v2(context.get(), columns);
    ASSERT_OK(expected_or.status());
    auto expected = std::move(expected_or).value();
    constexpr int thread_count{8};
    std::vector<std::unique_ptr<FunctionContext>> thread_contexts;
    std::vector<std::thread> threads;
    for (int thread = 0; thread < thread_count; ++thread) {
        thread_contexts.emplace_back(context->clone(nullptr));
        EXPECT_EQ(context->get_function_state(FunctionContext::FRAGMENT_LOCAL),
                  thread_contexts.back()->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    }
    for (const auto& thread_context : thread_contexts) {
        threads.emplace_back([&, thread_context = thread_context.get()] {
            const auto prepare_status =
                    CelonisCreateAlignment::create_alignment_v2_prepare(thread_context, FunctionContext::THREAD_LOCAL);
            if (!prepare_status.ok()) {
                ADD_FAILURE() << prepare_status;
                return;
            }
            DeferOp close_thread([thread_context] {
                CelonisCreateAlignment::create_alignment_close(thread_context, FunctionContext::THREAD_LOCAL);
            });
            const auto result = CelonisCreateAlignment::create_alignment_v2(thread_context, columns);
            if (!result.ok()) {
                ADD_FAILURE() << result.status();
                return;
            }
            EXPECT_EQ(expected->debug_string(), result.value()->debug_string());
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}

} // namespace
} // namespace starrocks
