// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.sql.analyzer;

import com.starrocks.analysis.Expr;
import com.starrocks.catalog.StructField;
import com.starrocks.catalog.StructType;
import com.starrocks.catalog.Type;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.utframe.UtFrameUtils;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.List;

import static com.starrocks.sql.analyzer.AnalyzeTestUtil.analyzeFail;
import static com.starrocks.sql.analyzer.AnalyzeTestUtil.analyzeSuccess;

public class CelonisCreateAlignmentV2Test {
    private static final long ALL_FIELDS_MASK = 140737488355327L;
    private static final List<String> FIELD_NAMES = Arrays.asList(
            "alignment_model_vertex_id",
            "alignment_vertex_label",
            "alignment_move_type",
            "alignment_activity_index",
            "alignment_deviation_category",
            "SYNC_EDGE_model_vertex_id",
            "SYNC_EDGE_vertex_label",
            "SYNC_EDGE_move_type",
            "SYNC_EDGE_deviation_category",
            "SYNC_EDGE_edge_class",
            "SYNC_EDGE_alignment_index",
            "MODEL_EDGE_model_vertex_id",
            "MODEL_EDGE_vertex_label",
            "MODEL_EDGE_move_type",
            "MODEL_EDGE_deviation_category",
            "MODEL_EDGE_edge_class",
            "MODEL_EDGE_alignment_index",
            "SKIP_EDGE_model_vertex_id",
            "SKIP_EDGE_vertex_label",
            "SKIP_EDGE_move_type",
            "SKIP_EDGE_deviation_category",
            "SKIP_EDGE_edge_class",
            "SKIP_EDGE_alignment_index",
            "LOG_EDGE_model_vertex_id",
            "LOG_EDGE_vertex_label",
            "LOG_EDGE_move_type",
            "LOG_EDGE_deviation_category",
            "LOG_EDGE_edge_class",
            "LOG_EDGE_alignment_index",
            "UNMAPPED_EDGE_model_vertex_id",
            "UNMAPPED_EDGE_vertex_label",
            "UNMAPPED_EDGE_move_type",
            "UNMAPPED_EDGE_deviation_category",
            "UNMAPPED_EDGE_edge_class",
            "UNMAPPED_EDGE_alignment_index",
            "MISSING_VIOLATION_model_vertex_id",
            "MISSING_VIOLATION_vertex_label",
            "MISSING_VIOLATION_move_type",
            "MISSING_VIOLATION_deviation_category",
            "MISSING_VIOLATION_edge_class",
            "MISSING_VIOLATION_alignment_index",
            "EXCLUSIVE_VIOLATION_model_vertex_id",
            "EXCLUSIVE_VIOLATION_vertex_label",
            "EXCLUSIVE_VIOLATION_move_type",
            "EXCLUSIVE_VIOLATION_deviation_category",
            "EXCLUSIVE_VIOLATION_edge_class",
            "EXCLUSIVE_VIOLATION_alignment_index");

    @BeforeAll
    public static void beforeClass() throws Exception {
        UtFrameUtils.createMinStarRocksCluster();
        AnalyzeTestUtil.init();
    }

    @Test
    public void testEverySingleFieldMask() {
        Assertions.assertEquals(47, FIELD_NAMES.size());
        for (int bit = 0; bit < FIELD_NAMES.size(); ++bit) {
            StructType type = analyzeV2Type(Long.toString(1L << bit));
            Assertions.assertEquals(1, type.getFields().size(), "bit " + bit);
            StructField field = type.getField(0);
            Assertions.assertEquals(FIELD_NAMES.get(bit), field.getName(), "bit " + bit);
            Assertions.assertEquals(expectedType(field.getName()), field.getType(), "bit " + bit);
        }
    }

    @Test
    public void testRepresentativeMasksUseCanonicalOrder() {
        assertFields(analyzeV2Type("28"), "alignment_move_type", "alignment_activity_index",
                "alignment_deviation_category");
        assertFields(analyzeV2Type("1576"), "alignment_activity_index", "SYNC_EDGE_model_vertex_id",
                "SYNC_EDGE_edge_class", "SYNC_EDGE_alignment_index");
        assertFields(analyzeV2Type(Long.toString((1L << 46) | 1L)), "alignment_model_vertex_id",
                "EXCLUSIVE_VIOLATION_alignment_index");
    }

    @Test
    public void testAllFieldsMatchesLegacySchema() {
        StructType legacyType = analyzeType("celonis_create_alignment(cast([] as array<varchar>), '{}')");
        StructType v2Type = analyzeV2Type(Long.toString(ALL_FIELDS_MASK));
        Assertions.assertEquals(legacyType, v2Type);
        Assertions.assertEquals(47, v2Type.getFields().size());
    }

    @Test
    public void testSelectedAndOmittedFieldResolution() {
        analyzeSuccess("select celonis_create_alignment_v2(cast([] as array<varchar>), '{}', 4)." +
                "alignment_move_type");
        analyzeFail("select celonis_create_alignment_v2(cast([] as array<varchar>), '{}', 4)." +
                "alignment_activity_index", "alignment_activity_index' cannot be resolved");
        assertFields(analyzeV2Type("cast(28 as bigint)"), "alignment_move_type", "alignment_activity_index",
                "alignment_deviation_category");
    }

    @Test
    public void testInvalidMasksAndSignature() {
        analyzeFail(v2Sql("0"), "must be positive");
        analyzeFail(v2Sql("-1"), "must be positive");
        analyzeFail(v2Sql("140737488355328"), "contains unknown bits");
        analyzeFail(v2Sql("NULL"), "must not be NULL");
        analyzeFail(v2Sql("1 + 3"), "must be an integer literal or CAST(integer literal AS BIGINT)");
        analyzeFail(v2Sql("cast(257 as tinyint)"),
                "must be an integer literal or CAST(integer literal AS BIGINT)");
        analyzeFail(v2Sql("cast(cast(28 as tinyint) as bigint)"),
                "must be an integer literal or CAST(integer literal AS BIGINT)");
        analyzeFail("select celonis_create_alignment_v2(cast([] as array<varchar>), '{}', v1) from t0",
                "must be a constant BIGINT");
        analyzeFail("select celonis_create_alignment_v2(cast([] as array<varchar>), '{}')",
                "No matching function with signature");
        analyzeFail("select celonis_create_alignment_v2(cast([] as array<varchar>), '{}', 1, 2)",
                "No matching function with signature");
    }

    private static StructType analyzeV2Type(String mask) {
        return analyzeType("celonis_create_alignment_v2(cast([] as array<varchar>), '{}', " + mask + ")");
    }

    private static StructType analyzeType(String expression) {
        QueryStatement statement = (QueryStatement) analyzeSuccess("select " + expression);
        Expr output = statement.getQueryRelation().getOutputExpression().get(0);
        Assertions.assertTrue(output.getType() instanceof StructType);
        return (StructType) output.getType();
    }

    private static String v2Sql(String mask) {
        return "select celonis_create_alignment_v2(cast([] as array<varchar>), '{}', " + mask + ")";
    }

    private static Type expectedType(String fieldName) {
        return fieldName.endsWith("vertex_label") || fieldName.endsWith("move_type") ||
                fieldName.endsWith("deviation_category") ? Type.ARRAY_VARCHAR : Type.ARRAY_BIGINT;
    }

    private static void assertFields(StructType type, String... expectedNames) {
        Assertions.assertEquals(expectedNames.length, type.getFields().size());
        for (int i = 0; i < expectedNames.length; ++i) {
            Assertions.assertEquals(expectedNames[i], type.getField(i).getName());
            Assertions.assertEquals(expectedType(expectedNames[i]), type.getField(i).getType());
        }
    }
}
