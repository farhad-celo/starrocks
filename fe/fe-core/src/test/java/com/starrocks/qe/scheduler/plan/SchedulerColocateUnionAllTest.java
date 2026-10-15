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

package com.starrocks.qe.scheduler.plan;

import com.starrocks.planner.OlapScanNode;
import com.starrocks.qe.DefaultCoordinator;
import com.starrocks.qe.scheduler.SchedulerTestBase;
import com.starrocks.qe.scheduler.dag.ExecutionFragment;
import com.starrocks.qe.scheduler.dag.FragmentInstance;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.stream.Collectors;

public class SchedulerColocateUnionAllTest extends SchedulerTestBase {
    @BeforeAll
    public static void beforeAll() throws Exception {
        SchedulerTestBase.beforeClass();
        for (String name : new String[] {"sua_f1", "sua_f2", "sua_m"}) {
            starRocksAssert.withTable("CREATE TABLE `" + name + "` (\n" +
                    "  `shard` int NULL,\n" +
                    "  `a` int NULL\n" +
                    ") ENGINE=OLAP\n" +
                    "DUPLICATE KEY(`shard`)\n" +
                    "DISTRIBUTED BY HASH(`shard`) BUCKETS 12\n" +
                    "PROPERTIES (\"replication_num\" = \"1\", \"colocate_with\" = \"sua_group\");");
        }
    }

    @AfterAll
    public static void afterAll() throws Exception {
        SchedulerTestBase.afterClass();
    }

    @Test
    public void testChildPrunedToOneBucketGetsPerDriverScanRangesEverywhere() throws Exception {
        connectContext.getSessionVariable().setEnableColocateUnionAll(true);
        // assign scan ranges per driver: 12 buckets over 3 BEs is 4 per BE, more than pipeline_dop / 2
        int pipelineDop = connectContext.getSessionVariable().getPipelineDop();
        connectContext.getSessionVariable().setPipelineDop(4);
        try {
            // sua_f2 is pruned to the bucket of shard = 3, which lives on one of the three BEs only
            String sql = "with src as (select shard, a from sua_f1 union all " +
                    "select shard, a from sua_f2 where shard = 3)\n" +
                    "select /*+SET_VAR(enable_tablet_internal_parallel=false)*/ src.* " +
                    "from src join [unreorder] sua_m on src.shard = sua_m.shard";
            DefaultCoordinator coordinator = startScheduling(sql);
            List<ExecutionFragment> colocateFragments = coordinator.getExecutionDAG().getFragmentsInPreorder()
                    .stream()
                    .filter(f -> f.getPlanFragment().hasColocateUnionAll())
                    .collect(Collectors.toList());
            Assertions.assertEquals(1, colocateFragments.size());
            ExecutionFragment fragment = colocateFragments.get(0);
            List<Integer> olapScanIds = fragment.getScanNodes().stream()
                    .filter(scan -> scan instanceof OlapScanNode)
                    .map(scan -> scan.getId().asInt())
                    .collect(Collectors.toList());
            Assertions.assertEquals(3, olapScanIds.size());
            Assertions.assertTrue(fragment.getInstances().size() > 1);
            boolean anyInstanceWithoutF2Ranges = false;
            for (FragmentInstance instance : fragment.getInstances()) {
                Assertions.assertFalse(instance.getNode2DriverSeqToScanRanges().isEmpty(),
                        "scan ranges should be assigned per driver on " + instance);
                for (Integer scanId : olapScanIds) {
                    Assertions.assertTrue(instance.getNode2DriverSeqToScanRanges().containsKey(scanId),
                            "scan " + scanId + " has no per-driver scan ranges on " + instance);
                }
                anyInstanceWithoutF2Ranges |= instance.getNode2DriverSeqToScanRanges().values().stream()
                        .anyMatch(perDriver -> perDriver.values().stream().allMatch(List::isEmpty));
            }
            Assertions.assertTrue(anyInstanceWithoutF2Ranges, "the pruned child should be empty on some instance");
        } finally {
            connectContext.getSessionVariable().setEnableColocateUnionAll(false);
            connectContext.getSessionVariable().setPipelineDop(pipelineDop);
        }
    }
}
