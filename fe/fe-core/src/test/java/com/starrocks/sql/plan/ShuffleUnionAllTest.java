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

import com.starrocks.catalog.OlapTable;
import com.starrocks.common.FeConstants;
import com.starrocks.server.GlobalStateMgr;
import org.apache.commons.lang.StringUtils;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class ShuffleUnionAllTest extends PlanTestBase {

    @BeforeAll
    public static void beforeClass() throws Exception {
        PlanTestBase.beforeClass();
        FeConstants.runningUnitTest = true;
        // not colocated with each other, and not bucketed on the join key
        for (String name : new String[] {"su_f1", "su_f2", "su_f3", "su_m"}) {
            starRocksAssert.withTable("CREATE TABLE `" + name + "` (\n" +
                    "  `shard` int NULL,\n" +
                    "  `a` int NULL,\n" +
                    "  `b` int NULL\n" +
                    ") ENGINE=OLAP\n" +
                    "DUPLICATE KEY(`shard`)\n" +
                    "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                    "PROPERTIES (\"replication_num\" = \"1\");");
        }
        // big facts and a mapping table too big to broadcast, so the anti join is PARTITIONED
        for (String name : new String[] {"su_f1", "su_f2", "su_f3"}) {
            setTableStatistics(table(name), 4_000_000_000L);
        }
        setTableStatistics(table("su_m"), 200_000_000L);
        // a mapping table small enough to broadcast
        starRocksAssert.withTable("CREATE TABLE `su_small` (\n" +
                "  `shard` int NULL,\n" +
                "  `a` int NULL,\n" +
                "  `b` int NULL\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`shard`)\n" +
                "DISTRIBUTED BY HASH(`shard`) BUCKETS 4\n" +
                "PROPERTIES (\"replication_num\" = \"1\");");
        setTableStatistics(table("su_small"), 10_000_000L);
    }

    private static OlapTable table(String name) {
        return (OlapTable) GlobalStateMgr.getCurrentState().getLocalMetastore().getTable("test", name);
    }

    @BeforeEach
    public void enable() {
        connectContext.getSessionVariable().setEnableShuffleUnionAll(true);
    }

    @AfterEach
    public void reset() {
        connectContext.getSessionVariable().setEnableShuffleUnionAll(false);
    }

    // the join key is a column every branch outputs, at the same union position
    private static final String ANTI_JOIN_SQL = "with src as (\n" +
            "  select a, b from su_f1 where a is not null\n" +
            "  union all select a, b from su_f2 where a is not null\n" +
            "  union all select a, b from su_f3)\n" +
            "select distinct src.a, src.b from src left anti join [unreorder] su_m on src.a = su_m.a";

    // the sink of a random exchange; "PARTITION: RANDOM" in a fragment header is not an exchange
    private static final String RANDOM_EXCHANGE = "\n    RANDOM\n";

    @Test
    public void testBranchesShuffleStraightToTheJoin() throws Exception {
        String plan = getFragmentPlan(ANTI_JOIN_SQL);
        assertContains(plan, "LEFT ANTI JOIN (PARTITIONED)");
        // no branch goes round-robin to a union fragment first
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
        // the union runs in the join's fragment, fed by one hash exchange per branch
        int join = plan.indexOf("LEFT ANTI JOIN (PARTITIONED)");
        int fragmentStart = plan.lastIndexOf("PLAN FRAGMENT", join);
        int fragmentEnd = plan.indexOf("PLAN FRAGMENT", join);
        String joinFragment = plan.substring(fragmentStart, fragmentEnd < 0 ? plan.length() : fragmentEnd);
        assertContains(joinFragment, ":UNION");
        // 3 branches + the mapping table, each hash partitioned once
        Assertions.assertEquals(4, StringUtils.countMatches(plan, "HASH_PARTITIONED: "), plan);
    }

    @Test
    public void testSmallBuildSideIsStillBroadcast() throws Exception {
        // 12B probe rows against a 10M-row build side, within broadcast_row_limit: the shuffled union must not
        // crowd out broadcast
        String sql = "with src as (\n" +
                "  select a, b from su_f1 union all select a, b from su_f2 union all select a, b from su_f3)\n" +
                "select distinct src.a from src left anti join [unreorder] su_small on src.a = su_small.a";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "LEFT ANTI JOIN (BROADCAST)");
        assertContains(plan, RANDOM_EXCHANGE);
    }

    @Test
    public void testBuildSideTooBigToBroadcastKeepsShuffledUnion() throws Exception {
        // the 200M-row build side is over broadcast_row_limit, so the shuffled union stays an option and wins
        String sql = "with src as (\n" +
                "  select a, b from su_f1 where a is not null union all select a, b from su_f2)\n" +
                "select distinct src.a from src left anti join [unreorder] su_m on src.a = su_m.a";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "LEFT ANTI JOIN (PARTITIONED)");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
    }

    @Test
    public void testDisabledByDefault() throws Exception {
        connectContext.getSessionVariable().setEnableShuffleUnionAll(false);
        String plan = getFragmentPlan(ANTI_JOIN_SQL);
        // branches go round-robin to the union, whose output is shuffled once more
        assertContains(plan, RANDOM_EXCHANGE);
    }

    @Test
    public void testComputedJoinKeyAboveUnionIsNotShuffled() throws Exception {
        // the join key is computed above the union, so no union position holds it
        String sql = "with src as (\n" +
                "  select a, b from su_f1 union all select a, b from su_f2)\n" +
                "select src.a from src left anti join [unreorder] su_m on src.a + src.b = su_m.a";
        String plan = getFragmentPlan(sql);
        assertContains(plan, RANDOM_EXCHANGE);
    }

    @Test
    public void testJoinKeyComputedInEachBranchIsShuffled() throws Exception {
        // the workaround for a computed key: compute it in every branch
        String sql = "with src as (\n" +
                "  select a, b, a + b h from su_f1 union all select a, b, a + b from su_f2)\n" +
                "select src.a from src left anti join [unreorder] su_m on src.h = su_m.a";
        String plan = getFragmentPlan(sql);
        assertContains(plan, "LEFT ANTI JOIN (PARTITIONED)");
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
    }

    @Test
    public void testAggregationAboveUnionIsShuffledOnce() throws Exception {
        String sql = "select a, count(*) from (\n" +
                "  select a from su_f1 union all select a from su_f2) t group by a";
        String plan = getFragmentPlan(sql);
        Assertions.assertFalse(plan.contains(RANDOM_EXCHANGE), plan);
    }

    @Test
    public void testPlainUnionAllUnchanged() throws Exception {
        // nothing above needs a hash distribution
        String plan = getFragmentPlan("select a, b from su_f1 union all select a, b from su_f2");
        assertContains(plan, RANDOM_EXCHANGE);
    }
}
