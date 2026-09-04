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

package com.starrocks.sql.plan;

import com.starrocks.catalog.ColumnId;
import com.starrocks.common.FeConstants;
import com.starrocks.sql.optimizer.statistics.IDictManager;
import com.starrocks.utframe.StarRocksAssert;
import mockit.Expectations;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

public class LowCardinalityCelonisFunctionsTest extends PlanTestBase {

    @BeforeAll
    public static void beforeClass() throws Exception {
        PlanTestBase.beforeClass();
        StarRocksAssert starRocksAssert = new StarRocksAssert(connectContext);
        starRocksAssert.withTable("""
                CREATE TABLE T (
                    KEY_COL             INTEGER NOT NULL,
                    VARCHAR_COL         VARCHAR(25),
                    VARCHAR_COL2         VARCHAR(25),
                    ARRAY_VARCHAR_COL   ARRAY<VARCHAR(40)>,
                    INTEGER_COL         INTEGER)
                ENGINE=OLAP
                DUPLICATE KEY(`KEY_COL`)
                COMMENT "OLAP"
                DISTRIBUTED by HASH(`KEY_COL`) BUCKETS 1
                PROPERTIES (
                    "replication_num" = "1",
                    "in_memory" = "false"
                );
                """);

        starRocksAssert.withTable("""
                CREATE TABLE TestActivityTimestampTable (
                    KEY_COL          INTEGER NOT NULL,
                    ACTIVITIES       ARRAY<VARCHAR(40)>,
                    TIMESTAMPS       ARRAY<BIGINT>)
                ENGINE=OLAP
                DUPLICATE KEY(`KEY_COL`)
                COMMENT "OLAP"
                DISTRIBUTED by HASH(`KEY_COL`) BUCKETS 1
                PROPERTIES (
                    "replication_num" = "1",
                    "in_memory" = "false"
                );
                """);
        starRocksAssert.withTable("""
                CREATE TABLE multi_input_functions_test_table (
                    KEY_COL          INTEGER NOT NULL,
                    string_array1    ARRAY<VARCHAR(40)>,
                    string_array2    ARRAY<VARCHAR(40)>,
                    string_array3    ARRAY<VARCHAR(40)>,
                    boolean_array    ARRAY<BOOLEAN>,
                    integer_array    ARRAY<INTEGER>)
                ENGINE=OLAP
                DUPLICATE KEY(`KEY_COL`)
                COMMENT "OLAP"
                DISTRIBUTED by HASH(`KEY_COL`) BUCKETS 1
                PROPERTIES (
                    "replication_num" = "1",
                    "in_memory" = "false"
                );
                """);

        FeConstants.USE_MOCK_DICT_MANAGER = true;
        connectContext.getSessionVariable().setSqlMode(2);
        connectContext.getSessionVariable().setEnableLowCardinalityOptimize(true);
        connectContext.getSessionVariable().setCboCteReuse(false);
        connectContext.getSessionVariable().setUseLowCardinalityOptimizeV2(true);
        connectContext.getSessionVariable().setEnableStructLowCardinalityOptimize(true);
        connectContext.getSessionVariable().setEnableMultiArrayAggLowCardinalityOptimize(true);
    }

    @AfterAll
    public static void afterClass() {
        FeConstants.USE_MOCK_DICT_MANAGER = false;
        connectContext.getSessionVariable().setSqlMode(0);
        connectContext.getSessionVariable().setEnableLowCardinalityOptimize(false);
        connectContext.getSessionVariable().setUseLowCardinalityOptimizeV2(false);
        connectContext.getSessionVariable().setEnableStructLowCardinalityOptimize(false);
        connectContext.getSessionVariable().setEnableMultiArrayAggLowCardinalityOptimize(false);
    }

    @Test
    public void testMultiArrayAggString1Stage() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR(new_planner_agg_stage='1', enable_multi_array_agg_v2='false') */
                MULTI_ARRAY_AGG(VARCHAR_COL, INTEGER_COL ORDER BY VARCHAR_COL, INTEGER_COL)
                FROM T
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("2:Project\n" +
                "  |  output columns:\n" +
                "  |  6 <-> named_struct[('col1', DictDecode(7: VARCHAR_COL, [<place-holder>], " +
                "8: multi_array_agg.col1[true]), 'col2', 8: multi_array_agg.col2[true]); args: VARCHAR,INVALID_TYPE," +
                "VARCHAR,INVALID_TYPE; result: struct<col1 array<varchar(25)>, col2 array<int(11)>>; args nullable:" +
                " true; result nullable: true]\n" +
                "  |  cardinality: 1\n" +
                "  |  \n" +
                "  1:AGGREGATE (update finalize)\n" +
                "  |  aggregate: multi_array_agg[([7: VARCHAR_COL, INT, true], [5: INTEGER_COL, INT, true], " +
                "[7: VARCHAR_COL, INT, true], [5: INTEGER_COL, INT, true]); args: INT,INT,INT,INT; result: " +
                "struct<col1 array<int(11)>, col2 array<int(11)>>; args nullable: true; result nullable: true]\n" +
                "  |  cardinality: 1"), plan);
    }

    @Test
    public void testMultiArrayAggString2Stage() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR(new_planner_agg_stage='2', enable_multi_array_agg_v2='false') */
                MULTI_ARRAY_AGG(VARCHAR_COL, VARCHAR_COL2 ORDER BY VARCHAR_COL, INTEGER_COL)
                FROM T
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("  4:Project\n" +
                "  |  output columns:\n" +
                "  |  6 <-> named_struct[('col1', DictDecode(7: VARCHAR_COL, [<place-holder>], " +
                "9: multi_array_agg.col1[true]), 'col2', DictDecode(8: VARCHAR_COL2, [<place-holder>], " +
                "9: multi_array_agg.col2[true])); args: VARCHAR,INVALID_TYPE,VARCHAR,INVALID_TYPE; result: " +
                "struct<col1 array<varchar(25)>, col2 array<varchar(25)>>; args nullable: true; " +
                "result nullable: true]\n" +
                "  |  cardinality: 1\n" +
                "  |  \n" +
                "  3:AGGREGATE (merge finalize)\n" +
                "  |  aggregate: multi_array_agg[([9: multi_array_agg, struct<col1 array<int(11)>, " +
                "col2 array<int(11)>, col3 array<int(11)>, col4 array<int(11)>>, true]); args: INT,INT,INT,INT; " +
                "result: struct<col1 array<int(11)>, col2 array<int(11)>>; args nullable: true; " +
                "result nullable: true]"), plan);
    }


    @Test
    public void testMultiArrayAggV2String1Stage() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR(new_planner_agg_stage='1', enable_multi_array_agg_v2='true') */
                MULTI_ARRAY_AGG(VARCHAR_COL, INTEGER_COL ORDER BY VARCHAR_COL, INTEGER_COL)
                FROM T
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("2:Project\n" +
                "  |  output columns:\n" +
                "  |  6 <-> named_struct[('col1', DictDecode(7: VARCHAR_COL, [<place-holder>], " +
                "8: multi_array_agg.col1[true]), 'col2', 8: multi_array_agg.col2[true]); args: VARCHAR,INVALID_TYPE," +
                "VARCHAR,INVALID_TYPE; result: struct<col1 array<varchar(25)>, col2 array<int(11)>>; args nullable:" +
                " true; result nullable: true]\n" +
                "  |  cardinality: 1\n" +
                "  |  \n" +
                "  1:AGGREGATE (update finalize)\n" +
                "  |  aggregate: multi_array_agg[([7: VARCHAR_COL, INT, true], [5: INTEGER_COL, INT, true], " +
                "[7: VARCHAR_COL, INT, true], [5: INTEGER_COL, INT, true]); args: INT,INT,INT,INT; result: " +
                "struct<col1 array<int(11)>, col2 array<int(11)>>; args nullable: true; result nullable: true]\n" +
                "  |  cardinality: 1"), plan);
    }

    @Test
    public void testMultiArrayAggV2String2Stage() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR(new_planner_agg_stage='2', enable_multi_array_agg_v2='true') */
                MULTI_ARRAY_AGG(VARCHAR_COL, VARCHAR_COL2 ORDER BY VARCHAR_COL, INTEGER_COL)
                FROM T
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("  4:Project\n" +
                "  |  output columns:\n" +
                "  |  6 <-> named_struct[('col1', DictDecode(7: VARCHAR_COL, [<place-holder>], " +
                "9: multi_array_agg.col1[true]), 'col2', DictDecode(8: VARCHAR_COL2, [<place-holder>], " +
                "9: multi_array_agg.col2[true])); args: VARCHAR,INVALID_TYPE,VARCHAR,INVALID_TYPE; result: " +
                "struct<col1 array<varchar(25)>, col2 array<varchar(25)>>; args nullable: true; " +
                "result nullable: true]\n" +
                "  |  cardinality: 1\n" +
                "  |  \n" +
                "  3:AGGREGATE (merge finalize)\n" +
                "  |  aggregate: multi_array_agg[([9: multi_array_agg, VARBINARY, true]); args: INT,INT,INT,INT; " +
                "result: struct<col1 array<int(11)>, col2 array<int(11)>>; args nullable: true; " +
                "result nullable: true]"), plan);
        Assertions.assertTrue(plan.contains("  1:AGGREGATE (update serialize)\n" +
                "  |  aggregate: multi_array_agg[([7: VARCHAR_COL, INT, true], [8: VARCHAR_COL2, INT, true], " +
                "[7: VARCHAR_COL, INT, true], [5: INTEGER_COL, INT, true]); args: INT,INT,INT,INT; result: " +
                "VARBINARY; args nullable: true; result nullable: true]\n" +
                "  |  cardinality: 1\n" +
                "  |  "), plan);
    }

    @Test
    public void testShortenedVariant() throws Exception {
        String sql = """
            SELECT /*+ SET_VAR(enable_shortened_variant_low_cardinality_optimize, "true") */
                CELONIS_SHORTENED_VARIANT(ACTIVITIES, 3)
            FROM TestActivityTimestampTable
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains(
                "DictDecode(5: ACTIVITIES, [<place-holder>], celonis_shortened_variant(5: ACTIVITIES, 3))"), plan);
    }

    @Test
    public void testSortedFirstLast() throws Exception {
        String sql = """
            SELECT /*+ SET_VAR(new_planner_agg_stage='2', enable_multi_array_agg_v2='true') */
                CELONIS_SORTED_FIRST(ARRAY_VARCHAR_COL ORDER BY VARCHAR_COL, VARCHAR_COL2),
                CELONIS_SORTED_LAST(INTEGER_COL ORDER BY VARCHAR_COL, INTEGER_COL)
            FROM T
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("Global Dict Exprs:\n" +
                "    11: DictDefine(10: ARRAY_VARCHAR_COL, [<place-holder>])\n" +
                "\n" +
                "  4:Decode\n" +
                "  |  <dict id 11> : <string id 6>\n" +
                "  |  cardinality: 1\n" +
                "  |  \n" +
                "  3:AGGREGATE (merge finalize)\n" +
                "  |  aggregate: celonis_sorted_first[([11: celonis_sorted_first, VARBINARY, true]); " +
                "args: INVALID_TYPE,INT,INT; result: ARRAY<INT>; args nullable: true; result nullable: true], " +
                "celonis_sorted_last[([7: celonis_sorted_last, VARBINARY, true]); args: INT,INT,INT; result: INT; " +
                "args nullable: true; result nullable: true]"), plan);
    }

    @Test
    public void testSortedFirstLastNonLowCardInput() throws Exception {
        String sql = """
            SELECT /*+ SET_VAR(new_planner_agg_stage='2', enable_multi_array_agg_v2='true') */
                CELONIS_SORTED_FIRST(VARCHAR_COL2 ORDER BY VARCHAR_COL)
            FROM T
                """;
        IDictManager dictManager = IDictManager.getInstance();
        new Expectations(dictManager) {
            {
                dictManager.hasGlobalDict(anyLong, ColumnId.create("VARCHAR_COL2"), anyLong);
                result = false;
            }
        };
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("celonis_sorted_first[([3: VARCHAR_COL2, VARCHAR, true], " +
                "[7: VARCHAR_COL, INT, true])"), plan);
    }

    @Test
    public void testIndexActivity() throws Exception {
        String sql = """
                select CELONIS_INDEX_ACTIVITY(ACTIVITIES, 'INDEX_ACTIVITY_TYPE', 'FORWARD')
                FROM TestActivityTimestampTable
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains(
                "celonis_index_activity[([5: ACTIVITIES, ARRAY<INT>, true], 'INDEX_ACTIVITY_TYPE', 'FORWARD')"), plan);
    }

    @Test
    public void testCalcCrop() throws Exception {
        String sql = """
                select /*+ SET_VAR('enable_calc_crop_low_cardinality_optimize', 'true') */
                CELONIS_CALC_CROP(ACTIVITIES, 'a', 'ALL', 'b', 'ALL')
                FROM TestActivityTimestampTable
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains(
                "celonis_calc_crop(5: ACTIVITIES, dict_encode('a', 5), 'ALL', dict_encode('b', 5), 'ALL')"), plan);
    }

    @Test
    public void testCalcCropToNull() throws Exception {
        String sql = """
                select /*+ SET_VAR('enable_calc_crop_low_cardinality_optimize', 'true') */
                CELONIS_CALC_CROP_TO_NULL(ACTIVITIES, 'a', 'ALL', 'b', 'ALL')
                FROM TestActivityTimestampTable
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains(
                "DictDecode(5: ACTIVITIES, [<place-holder>], celonis_calc_crop_to_null(5: ACTIVITIES, " +
                        "dict_encode('a', 5), 'ALL', dict_encode('b', 5), 'ALL'))"), plan);
    }

    @Test
    public void testMatchStringsWithEmptyListParam() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR('enable_match_activities_low_cardinality_optimize', 'true') */
                CELONIS_MATCH_ACTIVITIES(ACTIVITIES, ['a'], [], [], [], [], [])
                FROM TestActivityTimestampTable
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains(
                "celonis_match_activities(5: ACTIVITIES, [dict_encode('a', 5)], CAST([] AS ARRAY<INT>), " +
                        "CAST([] AS ARRAY<INT>), CAST([] AS ARRAY<INT>), CAST([] AS ARRAY<INT>), " +
                        "CAST([] AS ARRAY<INT>))"), plan);
    }

    @Test
    public void testSupportColumns() throws Exception {
        // Inner ACTIVITIES[1] is dictified, outer one is not.
        String sql = """
                WITH T AS (SELECT ACTIVITIES, ACTIVITIES[1] f FROM TestActivityTimestampTable ORDER BY 1)
                SELECT f, ACTIVITIES[1], ARRAY_AGG(ACTIVITIES) FROM T GROUP BY ACTIVITIES, f
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains(
                "  7:Decode\n" +
                        "  |  <dict id 9> : <string id 4>\n" +
                        "  |  cardinality: 1\n" +
                        "  |  \n" +
                        "  6:Project\n" +
                        "  |  output columns:\n" +
                        "  |  5 <-> [5: array_agg, ARRAY<ARRAY<VARCHAR(40)>>, true]\n" +
                        "  |  6 <-> 2: ACTIVITIES[1]\n" +
                        "  |  9 <-> [9: expr, INT, true]"), plan);
    }

    @Test
    public void testArrayFilterDictifiedByNonDictified() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR('enable_multi_input_functions_low_cardinality_optimize', 'true') */
                ARRAY_FILTER(string_array1, boolean_array) FROM multi_input_functions_test_table
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains(
                "DictDecode(8: string_array1, [<place-holder>], array_filter(8: string_array1, 5: boolean_array))"),
                plan);

    }

    @Test
    public void testArrayFilterDictifiedByRewritten() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR('enable_multi_input_functions_low_cardinality_optimize', 'true') */
                ARRAY_FILTER(string_array1, CAST(ARRAY_SORT(string_array2) AS ARRAY<BOOLEAN>))
                FROM multi_input_functions_test_table
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains("DictDecode(8: string_array1, [<place-holder>], array_filter(8: " +
                        "string_array1, CAST(DictDecode(9: string_array2, [<place-holder>], " +
                        "array_sort(9: string_array2)) AS ARRAY<BOOLEAN>)))"),
                plan);
    }

    @Test
    public void testArraySortByDictifiedByDictifiedAndNonDictified() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR('enable_multi_input_functions_low_cardinality_optimize', 'true') */
                ARRAY_SORTBY(string_array1, string_array2, ARRAY_SORT(string_array3), boolean_array)
                FROM multi_input_functions_test_table
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("DictDecode(8: string_array1, [<place-holder>], " +
                "array_sortby(8: string_array1, 9: string_array2, array_sort(10: string_array3), 5: boolean_array))\n"),
                plan);
        Assertions.assertTrue(plan.contains("dict_col=string_array1,string_array2,string_array3"), plan);
    }

    @Test
    public void testArraySortByNonDictifiedByDictifiedAndNonDictified() throws Exception {
        String sql = """
                SELECT /*+ SET_VAR('enable_multi_input_functions_low_cardinality_optimize', 'true') */
                ARRAY_SORTBY(string_array1, string_array2, ARRAY_SORT(string_array3), boolean_array)
                FROM multi_input_functions_test_table
                """;
        IDictManager dictManager = IDictManager.getInstance();
        new Expectations(dictManager) {
            {
                dictManager.hasGlobalDict(anyLong, ColumnId.create("string_array1"), anyLong);
                result = false;
            }
        };
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("array_sortby[([2: string_array1, ARRAY<VARCHAR(40)>, true]," +
                " [8: string_array2, ARRAY<INT>, true], array_sort[([9: string_array3, ARRAY<INT>, true]);" +
                " args: INVALID_TYPE; result: ARRAY<INT>; args nullable: true; result nullable: true], " +
                "[5: boolean_array, ARRAY<BOOLEAN>, true]); args: INVALID_TYPE,INVALID_TYPE,INVALID_TYPE; result:" +
                " ARRAY<VARCHAR(40)>; args nullable: true; result nullable: true]\n"), plan);
    }

    @Test
    public void testArraySortBySupportColumns() throws Exception {
        String sql = """
                WITH T AS (
                    SELECT /*+ SET_VAR('enable_multi_input_functions_low_cardinality_optimize', 'true') */
                    string_array1, string_array2, string_array3
                    FROM multi_input_functions_test_table
                    ORDER BY 1, 2, 3
                ), T2 AS (
                    SELECT string_array1, string_array2, string_array3, ARRAY_AGG(string_array1) tmp
                    FROM T
                    GROUP BY string_array1, string_array2, string_array3
                )
                SELECT ARRAY_SORTBY(string_array1, string_array2), tmp
                FROM T2
                """;
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("array_sortby[([2: string_array1, ARRAY<VARCHAR(40)>, true], " +
                "[10: string_array2, ARRAY<INT>, true]); args: INVALID_TYPE,INVALID_TYPE; result: " +
                "ARRAY<VARCHAR(40)>; args nullable: true; result nullable: true]"), plan);
        Assertions.assertTrue(plan.contains("  3:Decode\n" +
                "  |  <dict id 9> : <string id 2>"));
    }

    @Test
    public void testArraySortNestedInput() throws Exception {
        String sql = """
                SELECT ARRAY_SORTBY(IF(KEY_COL > 0, string_array1, string_array2), string_array3)
                FROM multi_input_functions_test_table
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains(
                "array_sortby(if(1: KEY_COL > 0, 2: string_array1, 3: string_array2), 8: string_array3)"), plan);
    }
    
    @Test
    public void testDuplicatedCteLambdaArgumentsGetDistinctIds() throws Exception {
        String sql = """
                WITH mapped_input AS (
                    SELECT KEY_COL, ARRAY_MAP(x -> UPPER(x), string_array1) AS mapped
                    FROM multi_input_functions_test_table
                )
                SELECT /*+ SET_VAR('enable_array_map_low_cardinality_optimize' = '%b') */
                left_copy.mapped c1, right_copy.mapped c2
                FROM mapped_input left_copy JOIN mapped_input right_copy
                    ON left_copy.KEY_COL = right_copy.KEY_COL
                """;
        String plan = getFragmentPlan(sql.formatted(true));
        Assertions.assertTrue(plan.contains("<slot 22> : array_map(<slot 23> -> upper(<slot 23>), 17: string_array1)"));
        Assertions.assertTrue(plan.contains("<slot 15> : array_map(<slot 7> -> upper(<slot 7>), 10: string_array1)"));

        plan = getFragmentPlan(sql.formatted(false));
        Assertions.assertTrue(plan.contains("<slot 15> : array_map(<slot 7> -> upper(<slot 7>), 10: string_array1)"));
        Assertions.assertTrue(plan.contains("<slot 22> : array_map(<slot 7> -> upper(<slot 7>), 17: string_array1)"));
    }

    @Test
    public void testImmutableProjectionIsNotWrittenBack() throws Exception {
        String sql = """
                select /*+ SET_VAR('cbo_cte_reuse', 'true') */
                avg(distinct t1b), sum(distinct t1b), count(distinct t1b, t1c)
                from test_all_type group by rollup(t1c, t1b)
                """;
        String plan = getFragmentPlan(sql);
        // Both markers confirm the query really did reach the immutable projection: MultiCastDataSinks
        // is the CTE producer, REPEAT_NODE the rollup underneath it.
        assertContains(plan, "MultiCastDataSinks");
        assertContains(plan, "REPEAT_NODE");
    }

    @Test
    public void testNestedMultiArgumentLambdaArgumentsGetDistinctIds() throws Exception {
        String sql = """
                WITH mapped_input AS (
                    SELECT KEY_COL,
                           ARRAY_MAP((x, y) -> ARRAY_JOIN(
                                         ARRAY_MAP((p, q) -> CONCAT(p, q, x, y), string_array3, string_array3),
                                         '-'),
                                     string_array1, string_array2) AS mapped
                    FROM multi_input_functions_test_table
                )
                SELECT /*+ SET_VAR('enable_array_map_low_cardinality_optimize' = 'true') */
                left_copy.mapped c1, right_copy.mapped c2
                FROM mapped_input left_copy JOIN mapped_input right_copy
                    ON left_copy.KEY_COL = right_copy.KEY_COL
                """;
        String plan = getFragmentPlan(sql);
        Assertions.assertTrue(plan.contains("array_map((<slot 26>, <slot 27>) -> array_join(array_map(" +
                "(<slot 28>, <slot 29>) -> concat(<slot 28>, <slot 29>, <slot 26>, <slot 27>), 22: string_array3," +
                " 22: string_array3), '-'), 20: string_array1, 21: string_array2)\n"), plan);
        Assertions.assertTrue(plan.contains("array_map((<slot 7>, <slot 8>) -> array_join(array_map(" +
                "(<slot 9>, <slot 10>) -> concat(<slot 9>, <slot 10>, <slot 7>, <slot 8>), 15: string_array3, " +
                "15: string_array3), '-'), 13: string_array1, 14: string_array2)\n"), plan);
    }
}
