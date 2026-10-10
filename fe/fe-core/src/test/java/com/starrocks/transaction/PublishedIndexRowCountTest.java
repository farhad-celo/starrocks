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

package com.starrocks.transaction;

import com.starrocks.catalog.LocalTablet;
import com.starrocks.catalog.MaterializedIndex;
import com.starrocks.catalog.Replica;
import com.starrocks.catalog.Replica.ReplicaState;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.OptionalLong;
import java.util.Set;

public class PublishedIndexRowCountTest {
    private static final long VERSION = 11L;

    private MaterializedIndex index;
    private Replica r11;
    private Replica r12;
    private Replica r21;
    private Replica r22;

    private static Replica replica(long id, long backendId) {
        return new Replica(id, backendId, VERSION, 0, 0L, 0L, ReplicaState.NORMAL, -1, VERSION);
    }

    @BeforeEach
    public void setUp() {
        index = new MaterializedIndex(1L);
        LocalTablet t1 = new LocalTablet(101L);
        LocalTablet t2 = new LocalTablet(102L);
        r11 = replica(1L, 1L);
        r12 = replica(2L, 2L);
        r21 = replica(3L, 1L);
        r22 = replica(4L, 2L);
        t1.addReplica(r11, false);
        t1.addReplica(r12, false);
        t2.addReplica(r21, false);
        t2.addReplica(r22, false);
        index.addTablet(t1, null, false);
        index.addTablet(t2, null, false);
    }

    @Test
    public void testSumsCountsProvenAtTheVersion() {
        r11.updateRowCountAtVersion(100L, VERSION);
        r21.updateRowCountAtVersion(50L, VERSION);
        Assertions.assertEquals(OptionalLong.of(150L),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of()));
    }

    @Test
    public void testEmptyUnlessEveryTabletProvesTheExactVersion() {
        r11.updateRowCountAtVersion(100L, VERSION);
        // the BE was already a version ahead: not a count for VERSION
        r21.updateRowCountAtVersion(60L, VERSION + 1);
        r22.updateRowCountAtVersion(40L, VERSION - 1);
        Assertions.assertEquals(OptionalLong.empty(),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of()));
    }

    @Test
    public void testSkipsUnhealthyReplicas() {
        r11.updateRowCountAtVersion(100L, VERSION);
        r21.updateRowCountAtVersion(50L, VERSION);
        Assertions.assertEquals(OptionalLong.empty(),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of(r21.getId())));

        r22.updateRowCountAtVersion(55L, VERSION);
        r22.setBad(true);
        Assertions.assertEquals(OptionalLong.empty(),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of(r21.getId())));

        r22.setBad(false);
        Assertions.assertEquals(OptionalLong.of(155L),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of(r21.getId())));

        r22.updateLastFailedVersion(VERSION + 1);
        Assertions.assertEquals(OptionalLong.empty(),
                DatabaseTransactionMgr.publishedIndexRowCount(index, VERSION, Set.of(r21.getId())));
    }
}
