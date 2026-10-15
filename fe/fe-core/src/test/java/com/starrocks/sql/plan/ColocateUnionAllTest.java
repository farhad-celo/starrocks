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

import com.starrocks.common.FeConstants;
import com.starrocks.qe.SessionVariable;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class ColocateUnionAllTest extends PlanTestBase {

    @BeforeAll
    public static void beforeClass() throws Exception {
        PlanTestBase.beforeClass();
        FeConstants.runningUnitTest = true;
        for (String name : new String[] {"cu_f1", "cu_f2", "cu_f3", "cu_f4"}) {
            starRocksAssert.withTable("CREATE TABLE `" + name + "` (\n" +
                    "  `shard` int NULL,\n" +
                    "  `a` int NULL,\n" +
                    "  `b` int NULL,\n" +
                    "  `c` int NULL\n" +
                    ") ENGINE=OLAP\n" +
                    "DUPLICATE KEY(`shard`)\n" +
                    "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                    "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"cu_group\");");
        }
        starRocksAssert.withTable("CREATE TABLE `cu_m` (\n" +
                "  `shard` int NULL,\n" +
                "  `k1` int NULL,\n" +
                "  `k2` int NULL,\n" +
                "  `k3` int NULL\n" +
                ") ENGINE=OLAP\n" +
                "UNIQUE KEY(`shard`, `k1`, `k2`, `k3`)\n" +
                "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"cu_group\");");
        for (String name : new String[] {"cu_s1", "cu_s2"}) {
            starRocksAssert.withTable("CREATE TABLE `" + name + "` (\n" +
                    "  `shard` int NULL,\n" +
                    "  `name` varchar(20) NULL\n" +
                    ") ENGINE=OLAP\n" +
                    "DUPLICATE KEY(`shard`)\n" +
                    "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                    "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"cu_group\");");
        }
        for (String name : new String[] {"cu_k1", "cu_k2"}) {
            starRocksAssert.withTable("CREATE TABLE `" + name + "` (\n" +
                    "  `k` varchar(20) NULL,\n" +
                    "  `v` int NULL\n" +
                    ") ENGINE=OLAP\n" +
                    "DUPLICATE KEY(`k`)\n" +
                    "DISTRIBUTED BY HASH(`k`) BUCKETS 4\n" +
                    "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"cu_str_group\");");
        }
        // same bucket count, other group
        starRocksAssert.withTable("CREATE TABLE `cu_other` (\n" +
                "  `shard` int NULL,\n" +
                "  `a` int NULL,\n" +
                "  `b` int NULL,\n" +
                "  `c` int NULL\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`shard`)\n" +
                "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"cu_other_group\");");
    }

    @BeforeEach
    public void enable() {
        connectContext.getSessionVariable().setEnableColocateUnionAll(true);
    }

    @AfterEach
    public void reset() {
        connectContext.getSessionVariable().setEnableColocateUnionAll(false);
    }

    // the sink of a random exchange; "PARTITION: RANDOM" in a fragment header is not an exchange
    private static final String RANDOM_EXCHANGE = "\n    RANDOM\n";

    private static final String ANTI_JOIN_SQL = "with src as (\n" +
            "  select shard, a k1, b k2, c k3 from cu_f1 where a is not null\n" +
            "  union all select shard, a, b, c from cu_f2 where b is not null\n" +
            "  union all select shard, a, b, c from cu_f3\n" +
            "  union all select shard, a, b, c from cu_f4)\n" +
            "select distinct src.shard, src.k1, src.k2, src.k3 from src left anti join [unreorder] cu_m\n" +
            "  on src.shard = cu_m.shard and murmur_hash3_32(src.k1, src.k2, src.k3) = " +
            "murmur_hash3_32(cu_m.k1, cu_m.k2, cu_m.k3)";

    @Test
    public void testAntiJoinWithUnionAllIsColocate() throws Exception {
        String plan = getFragmentPlan(ANTI_JOIN_SQL);
        assertContains(plan, "LEFT ANTI JOIN (COLOCATE)");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
        assertContains(plan, "local exchange type: DIRECT");
    }

    @Test
    public void testDisabledByDefault() throws Exception {
        connectContext.getSessionVariable().setEnableColocateUnionAll(false);
        String plan = getFragmentPlan(ANTI_JOIN_SQL);
        // the join mode depends on the table sizes (broadcast for these tiny tables); what matters is that the
        // union children are sent randomly and the join is not colocated
        Assertions.assertFalse(plan.contains("(COLOCATE)"), plan);
        assertContains(plan, RANDOM_EXCHANGE);
    }

    @Test
    public void testDisableColocateSetWins() throws Exception {
        boolean disableColocateSet = connectContext.getSessionVariable().isDisableColocateSet();
        connectContext.getSessionVariable().setDisableColocateSet(true);
        try {
            String plan = getFragmentPlan(ANTI_JOIN_SQL);
            Assertions.assertFalse(plan.contains("(COLOCATE)"), plan);
        } finally {
            connectContext.getSessionVariable().setDisableColocateSet(disableColocateSet);
        }
    }

    @Test
    public void testOtherGroupChildIsNotColocate() throws Exception {
        String sql = "with src as (\n" +
                "  select shard, a, b, c from cu_f1 union all select shard, a, b, c from cu_other)\n" +
                "select src.* from src join [unreorder] cu_m on src.shard = cu_m.shard";
        String plan = getFragmentPlan(sql);
        Assertions.assertFalse(plan.contains("(COLOCATE)"), plan);
    }

    @Test
    public void testNonBucketColumnPositionIsNotColocate() throws Exception {
        // the union's first column is shard for cu_f1 but a for cu_f2
        String sql = "with src as (\n" +
                "  select shard x, a y from cu_f1 union all select a, shard from cu_f2)\n" +
                "select src.* from src join [unreorder] cu_m on src.x = cu_m.shard";
        String plan = getFragmentPlan(sql);
        Assertions.assertFalse(plan.contains("(COLOCATE)"), plan);
    }

    @Test
    public void testAggregationOnPositionNotBucketedInEveryChild() throws Exception {
        // y is the bucket column of both children, x only of cu_f1: the join on y stays colocated, while grouping
        // by x must shuffle by x above the join
        String sql = "select x, count(*) from (\n" +
                "  select shard x, shard y from cu_f1 union all select a, shard from cu_f2) t\n" +
                "  join [unreorder] cu_m on t.y = cu_m.shard group by x";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "INNER JOIN (COLOCATE)");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
        // the fragment holding the colocate join is sent to the aggregation hashed by x alone, which is the slot
        // the aggregation groups by
        int join = plan.indexOf("INNER JOIN (COLOCATE)");
        String joinFragment = plan.substring(plan.lastIndexOf("PLAN FRAGMENT", join), join);
        java.util.regex.Matcher sink = java.util.regex.Pattern.compile(
                "STREAM DATA SINK\\s+EXCHANGE ID: \\d+\\s+HASH_PARTITIONED: (\\d+): [\\w$]+\\n")
                .matcher(joinFragment);
        Assertions.assertTrue(sink.find(), plan);
        java.util.regex.Matcher groupBy = java.util.regex.Pattern.compile(
                "output: count\\(\\*\\)\\n\\s+\\|\\s+group by: (\\d+): [\\w$]+\\n")
                .matcher(plan);
        Assertions.assertTrue(groupBy.find(), plan);
        Assertions.assertEquals(groupBy.group(1), sink.group(1), plan);
    }

    @Test
    public void testFourChildrenWithComputedColumnsAreColocate() throws Exception {
        // computed columns at positions the parent doesn't need don't matter
        String sql = "with src as (\n" +
                "  select shard, a + 1 v from cu_f1 union all select shard, b * 2 from cu_f2\n" +
                "  union all select shard, c from cu_f3 union all select shard, abs(a) from cu_f4)\n" +
                "select src.* from src join [unreorder] cu_m on src.shard = cu_m.shard";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "INNER JOIN (COLOCATE)");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
    }

    @Test
    public void testComputedBucketPositionIsNotColocate() throws Exception {
        // one child puts a computed column, not its bucket column, at the joined position
        String sql = "with src as (\n" +
                "  select shard from cu_f1 union all select shard + 1 from cu_f2 union all select shard from cu_f3)\n" +
                "select src.* from src join [unreorder] cu_m on src.shard = cu_m.shard";
        String plan = getFragmentPlan(sql);
        Assertions.assertFalse(plan.contains("(COLOCATE)"), plan);
    }

    @Test
    public void testChildPrunedToOneBucketIsColocate() throws Exception {
        // cu_f2 is pruned to one bucket; the other buckets just have no rows from it, colocation still holds
        String sql = "with src as (\n" +
                "  select shard, a from cu_f1 union all select shard, a from cu_f2 where shard = 3)\n" +
                "select src.* from src join [unreorder] cu_m on src.shard = cu_m.shard";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "INNER JOIN (COLOCATE)");
        assertContains(plan, "tabletRatio=1/4");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
    }

    @Test
    public void testColocateUnionAllKeepsGlobalDict() throws Exception {
        SessionVariable sessionVariable = connectContext.getSessionVariable();
        boolean useMockDictManager = FeConstants.USE_MOCK_DICT_MANAGER;
        long sqlMode = sessionVariable.getSqlMode();
        boolean enableLowCardinalityOptimize = sessionVariable.isEnableLowCardinalityOptimize();
        boolean useLowCardinalityOptimizeV2 = sessionVariable.isUseLowCardinalityOptimizeV2();
        FeConstants.USE_MOCK_DICT_MANAGER = true;
        sessionVariable.setSqlMode(2);
        sessionVariable.setEnableLowCardinalityOptimize(true);
        sessionVariable.setUseLowCardinalityOptimizeV2(true);
        try {
            String plan = getVerboseExplain("select shard, name from (\n" +
                    "  select shard, name from cu_s1 union all select shard, name from cu_s2) t " +
                    "group by shard");
            Assertions.assertTrue(plan.contains("  4:Decode\n" +
                    "  |  <dict id 11> : <string id 7>\n" +
                    "  |  cardinality: 1\n" +
                    "  |  \n" +
                    "  3:AGGREGATE (update finalize)\n" +
                    "  |  aggregate: any_value[([10: name, INT, true]); args: INT; result: INT; args nullable: true;" +
                    " result nullable: true]\n" +
                    "  |  group by: [5: shard, INT, true]\n" +
                    "  |  cardinality: 1\n" +
                    "  |  \n" +
                    "  0:UNION\n" +
                    "  |  output exprs:\n" +
                    "  |      [5, INT, true] | [10, INT, true]\n" +
                    "  |  child exprs:\n" +
                    "  |      [1: shard, INT, true] | [8: name, INT, true]\n" +
                    "  |      [3: shard, INT, true] | [9: name, INT, true]\n" +
                    "  |  pass-through-operands: all\n" +
                    "  |  local exchange type: DIRECT\n" +
                    "  |  cardinality: 2\n" +
                    "  |  \n" +
                    "  |----2:OlapScanNode\n" +
                    "  |       table: cu_s2, rollup: cu_s2\n" +
                    "  |       preAggregation: on\n" +
                    "  |       dict_col=name\n" +
                    "  |       partitionsRatio=1/1, tabletsRatio=4/4\n" +
                    "  |       tabletList=14517,14519,14521,14523\n" +
                    "  |       actualRows=0, avgRowSize=2.0\n" +
                    "  |       cardinality: 1\n" +
                    "  |    \n" +
                    "  1:OlapScanNode\n" +
                    "     table: cu_s1, rollup: cu_s1\n" +
                    "     preAggregation: on\n" +
                    "     dict_col=name\n" +
                    "     partitionsRatio=1/1, tabletsRatio=4/4\n" +
                    "     tabletList=14505,14507,14509,14511\n" +
                    "     actualRows=0, avgRowSize=2.0\n" +
                    "     cardinality: 1"), plan);
        } finally {
            sessionVariable.setSqlMode(sqlMode);
            sessionVariable.setEnableLowCardinalityOptimize(enableLowCardinalityOptimize);
            sessionVariable.setUseLowCardinalityOptimizeV2(useLowCardinalityOptimizeV2);
            FeConstants.USE_MOCK_DICT_MANAGER = useMockDictManager;
        }
    }

    @Test
    public void testUnionAllWithAggregationAbove() throws Exception {
        String sql = "select shard, count(*) from (\n" +
                "  select shard from cu_f1 union all select shard from cu_f2) t group by shard";
        String plan = getFragmentPlan(sql);
        // the scans feed the union directly and the aggregation runs in one phase, without a shuffle
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
        Assertions.assertFalse(plan.contains("HASH_PARTITIONED"), plan);
        assertContains(plan, "AGGREGATE (update finalize)");
        assertContains(plan, "local exchange type: DIRECT");
    }

    @Test
    public void testPlainUnionAllUnchanged() throws Exception {
        String plan = getFragmentPlan("select * from cu_f1 union all select * from cu_f2");
        assertContains(plan, RANDOM_EXCHANGE);
        Assertions.assertFalse(plan.contains("local exchange type: DIRECT"), plan);
    }
}
