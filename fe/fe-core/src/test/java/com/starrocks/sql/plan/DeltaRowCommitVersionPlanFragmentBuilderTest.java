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
import com.starrocks.analysis.Expr;
import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.SlotDescriptor;
import com.starrocks.analysis.SlotId;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.Type;
import com.starrocks.connector.GetRemoteFilesParams;
import com.starrocks.connector.RemoteFileInfo;
import com.starrocks.connector.delta.DeltaUtils;
import com.starrocks.planner.DeltaLakeScanNode;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.MetadataMgr;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.logical.LogicalDeltaLakeScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalDeltaLakeScanOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.thrift.TResultSinkType;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.actions.Format;
import io.delta.kernel.internal.actions.Metadata;
import mockit.Delegate;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import mockit.Mocked;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;

public class DeltaRowCommitVersionPlanFragmentBuilderTest {
    private static final String ENABLE_KEY = "delta.enableRowTracking";
    private static final String MATERIALIZED_KEY = "delta.rowTracking.materializedRowCommitVersionColumnName";

    @Test
    public void testDeltaScanRewritesRowCommitVersionAndFiltersPhysicalFieldNames(
            @Mocked GlobalStateMgr globalStateMgr, @Mocked MetadataMgr metadataMgr,
            @Mocked SnapshotImpl snapshot, @Mocked Metadata metadata, @Mocked Format format) {
        new MockUp<DeltaUtils>() {
            @Mock
            public void checkProtocolAndMetadata(io.delta.kernel.internal.actions.Protocol protocol,
                                                 Metadata metadata) {
            }
        };

        DeltaLakeTable table = new DeltaLakeTable(1, null, "db", "t",
                List.of(new Column("id", Type.INT)), List.of(), snapshot, "/tmp/table", null, 0);
        AtomicReference<GetRemoteFilesParams> capturedParams = new AtomicReference<>();

        new Expectations() {
            {
                snapshot.getMetadata();
                result = metadata;
                minTimes = 0;

                snapshot.getProtocol();
                result = null;
                minTimes = 0;

                snapshot.getVersion();
                result = 12L;
                minTimes = 0;

                metadata.getConfiguration();
                result = ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys");
                minTimes = 0;

                metadata.getFormat();
                result = format;
                minTimes = 0;

                format.getProvider();
                result = "parquet";
                minTimes = 0;

                GlobalStateMgr.getCurrentState().getMetadataMgr();
                result = metadataMgr;
                minTimes = 0;

                metadataMgr.getRemoteFiles((Table) table, (GetRemoteFilesParams) any);
                result = new Delegate<List<RemoteFileInfo>>() {
                    List<RemoteFileInfo> getRemoteFiles(Table requestedTable, GetRemoteFilesParams params) {
                        capturedParams.set(params);
                        return List.of();
                    }
                };
                minTimes = 0;
            }
        };

        ColumnRefFactory columnRefFactory = new ColumnRefFactory();
        ColumnRefOperator idRef = columnRefFactory.create("id", Type.INT, true);
        ColumnRefOperator rowCommitVersionRef = columnRefFactory.create(
                DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true);
        Column idColumn = new Column("id", Type.INT, true);
        Column rowCommitVersionColumn = new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true);
        PhysicalDeltaLakeScanOperator scanOperator = new PhysicalDeltaLakeScanOperator(
                new LogicalDeltaLakeScanOperator(table,
                        ImmutableMap.of(idRef, idColumn, rowCommitVersionRef, rowCommitVersionColumn),
                        ImmutableMap.of(idColumn, idRef, rowCommitVersionColumn, rowCommitVersionRef), -1, null));
        OptExpression optExpression = OptExpression.create(scanOperator);
        ConnectContext connectContext = new ConnectContext();
        connectContext.setQueryId(new UUID(1, 2));
        connectContext.setThreadLocalInfo();

        ExecPlan plan;
        try {
            plan = PlanFragmentBuilder.createPhysicalPlan(optExpression, connectContext,
                    List.of(rowCommitVersionRef), columnRefFactory,
                    List.of(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN), TResultSinkType.MYSQL_PROTOCAL, false);
        } finally {
            ConnectContext.remove();
        }

        Assertions.assertEquals(List.of("id"), capturedParams.get().getFieldNames());
        Assertions.assertEquals(1, plan.getScanNodes().size());
        Assertions.assertInstanceOf(DeltaLakeScanNode.class, plan.getScanNodes().get(0));

        Expr rewritten = plan.getColRefToExpr().get(rowCommitVersionRef);
        FunctionCallExpr coalesce = Assertions.assertInstanceOf(FunctionCallExpr.class, rewritten);
        Assertions.assertEquals(FunctionSet.COALESCE, coalesce.getFnName().getFunction());

        SlotDescriptor materializedSlot = plan.getDescTbl().getSlotDesc(new SlotId(rowCommitVersionRef.getId()));
        Assertions.assertEquals("_rcv_phys", materializedSlot.getColumn().getName());
        Assertions.assertTrue(plan.getDescTbl().getTupleDescs().stream()
                .flatMap(tupleDescriptor -> tupleDescriptor.getSlots().stream())
                .map(SlotDescriptor::getColumn)
                .anyMatch(column -> column != null
                        && DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN.equals(column.getName())));
    }
}
