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

import com.google.common.collect.ImmutableMap;
import com.starrocks.catalog.ColumnId;
import com.starrocks.common.FeConstants;
import com.starrocks.sql.optimizer.statistics.ColumnDict;
import com.starrocks.sql.optimizer.statistics.IDictManager;
import com.starrocks.utframe.StarRocksAssert;
import mockit.Expectations;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Optional;

public class LowCardinalityCelonisFunctionsTest extends PlanTestBase {

    @BeforeAll
    public static void beforeClass() throws Exception {
        PlanTestBase.beforeClass();
        StarRocksAssert starRocksAssert = new StarRocksAssert(connectContext);
        starRocksAssert.withTable("""
                CREATE TABLE T (
                    KEY_COL             INTEGER NOT NULL,
                    VARCHAR_COL         VARCHAR(25),
                    VARCHAR_COL2        VARCHAR(25),
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
                CREATE TABLE TestCelonisCalcThroughputTable (
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
    public void testCalcThroughput() throws Exception {
        String sql = """
                SELECT
                CELONIS_CALC_THROUGHPUT(ACTIVITIES, TIMESTAMPS, 'b' , 'd' , 'first' , 'last')
                FROM TestCelonisCalcThroughputTable
                """;
        IDictManager dictManager = IDictManager.getInstance();
        new Expectations(dictManager) {
            {
                dictManager.hasGlobalDict(anyLong, ColumnId.create("ACTIVITIES"), anyLong);
                result = true;
                dictManager.getGlobalDict(anyLong, ColumnId.create("ACTIVITIES"));
                ImmutableMap<ByteBuffer, Integer> data = ImmutableMap.<ByteBuffer, Integer>builder()
                        .put(ByteBuffer.wrap("a".getBytes(StandardCharsets.UTF_8)), 1)
                        .put(ByteBuffer.wrap("b".getBytes(StandardCharsets.UTF_8)), 2)
                        .put(ByteBuffer.wrap("c".getBytes(StandardCharsets.UTF_8)), 3)
                        .build();
                result = Optional.of(new ColumnDict(data, 0));
            }
        };
        String plan = getVerboseExplain(sql);
        Assertions.assertTrue(plan.contains("celonis_calc_throughput[([5: ACTIVITIES, ARRAY<INT>, true], " +
                "[3: TIMESTAMPS, ARRAY<BIGINT>, true], 2, NULL, 'first', 'last')"), plan);
    }
}