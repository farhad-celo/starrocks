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

package com.starrocks.connector.delta;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.starrocks.analysis.DescriptorTable;
import com.starrocks.analysis.Expr;
import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.SlotDescriptor;
import com.starrocks.analysis.SlotId;
import com.starrocks.analysis.SlotRef;
import com.starrocks.analysis.TupleDescriptor;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.Type;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.sql.common.StarRocksPlannerException;
import com.starrocks.sql.common.UnsupportedException;
import com.starrocks.sql.optimizer.operator.logical.LogicalDeltaLakeScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalDeltaLakeScanOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.thrift.TExpr;
import com.starrocks.thrift.TExprNodeType;
import com.starrocks.thrift.THdfsScanNode;
import com.starrocks.thrift.THdfsScanRange;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.actions.Metadata;
import io.delta.kernel.utils.FileStatus;
import mockit.Expectations;
import mockit.Mocked;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class DeltaRowCommitVersionTest {
    private static final String ENABLE_KEY = "delta.enableRowTracking";
    private static final String MATERIALIZED_KEY = "delta.rowTracking.materializedRowCommitVersionColumnName";

    private DeltaLakeTable tableWith(SnapshotImpl snapshot, Metadata metadata, Map<String, String> config) {
        new Expectations() {
            {
                snapshot.getMetadata();
                result = metadata;
                minTimes = 0;
                metadata.getConfiguration();
                result = config;
                minTimes = 0;
            }
        };
        return new DeltaLakeTable(1, "c", "db", "t", Lists.newArrayList(), Lists.newArrayList(),
                snapshot, "path", null, 0);
    }

    private static PhysicalDeltaLakeScanOperator scanOperatorWithRowCommitVersion(DeltaLakeTable table,
                                                                                 ColumnRefOperator rowCommitVersionRef) {
        Column rowCommitVersionColumn = new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true);
        return new PhysicalDeltaLakeScanOperator(
                new LogicalDeltaLakeScanOperator(table, ImmutableMap.of(rowCommitVersionRef, rowCommitVersionColumn),
                        ImmutableMap.of(rowCommitVersionColumn, rowCommitVersionRef), -1, null));
    }

    private static SlotDescriptor addRowCommitVersionSlot(DescriptorTable descTable, TupleDescriptor tupleDescriptor,
                                                         ColumnRefOperator rowCommitVersionRef) {
        SlotDescriptor materializedSlot = descTable.addSlotDescriptor(tupleDescriptor,
                new SlotId(rowCommitVersionRef.getId()));
        materializedSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true));
        return materializedSlot;
    }

    @Test
    public void testRowTrackingEnabled(@Mocked SnapshotImpl snapshot, @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));
        assertTrue(table.isRowTrackingEnabled());
        assertEquals("_rcv_phys", table.getMaterializedRowCommitVersionColumnName());
    }

    @Test
    public void testRowTrackingDisabled(@Mocked SnapshotImpl snapshot, @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata, ImmutableMap.of());
        assertFalse(table.isRowTrackingEnabled());
        assertNull(table.getMaterializedRowCommitVersionColumnName());
    }

    @Test
    public void testDefaultRowCommitVersionOnScanTask() {
        assertNull(new FileScanTask(null, 0, Map.of(), null).getDefaultRowCommitVersion());
        assertEquals(Long.valueOf(42L),
                new FileScanTask(null, 0, Map.of(), null, 42L).getDefaultRowCommitVersion());
    }

    @Test
    public void testGetDeltaRowTrackingColumns(@Mocked SnapshotImpl snapshot, @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));

        List<Column> columns = DeltaRowTrackingUtils.getDeltaRowTrackingColumns(table);

        assertEquals(1, columns.size());
        assertEquals(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, columns.get(0).getName());
        assertEquals(Type.BIGINT, columns.get(0).getType());
        assertTrue(columns.get(0).isAllowNull());
    }

    @Test
    public void testGetDeltaRowTrackingColumnsInjectsWhenRowTrackingDisabled(@Mocked SnapshotImpl snapshot,
                                                                             @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata, ImmutableMap.of());

        List<Column> columns = DeltaRowTrackingUtils.getDeltaRowTrackingColumns(table);

        assertEquals(1, columns.size());
        assertEquals(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, columns.get(0).getName());
    }

    @Test
    public void testGetDeltaRowTrackingColumnsSkipsExistingColumn(@Mocked SnapshotImpl snapshot,
                                                                  @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));
        table.addColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true));

        assertTrue(DeltaRowTrackingUtils.getDeltaRowTrackingColumns(table).isEmpty());
    }

    @Test
    public void testSetRowCommitVersionExtendedColumn() {
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        defaultSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT));

        THdfsScanRange scanRange = new THdfsScanRange();
        DeltaRowTrackingUtils.setRowCommitVersionExtendedColumn(scanRange,
                new FileScanTask(null, 0, Map.of(), null, 42L), tupleDescriptor);

        assertEquals(1, scanRange.getExtended_columnsSize());
        TExpr defaultExpr = scanRange.getExtended_columns().get(defaultSlot.getId().asInt());
        assertNotNull(defaultExpr);
        assertEquals(TExprNodeType.INT_LITERAL, defaultExpr.nodes.get(0).node_type);
        assertEquals(42L, defaultExpr.nodes.get(0).int_literal.value);
    }

    @Test
    public void testSetRowCommitVersionExtendedColumnPreservesExistingColumns() {
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        defaultSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT));

        int existingSlotId = defaultSlot.getId().asInt() + 1;
        TExpr existingExpr = new TExpr();
        THdfsScanRange scanRange = new THdfsScanRange();
        scanRange.setExtended_columns(Map.of(existingSlotId, existingExpr));

        DeltaRowTrackingUtils.setRowCommitVersionExtendedColumn(scanRange,
                new FileScanTask(null, 0, Map.of(), null, 42L), tupleDescriptor);

        assertEquals(2, scanRange.getExtended_columnsSize());
        assertSame(existingExpr, scanRange.getExtended_columns().get(existingSlotId));
        assertNotNull(scanRange.getExtended_columns().get(defaultSlot.getId().asInt()));
    }

    @Test
    public void testSetRowCommitVersionExtendedColumnFailsWhenDefaultAbsent() {
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        defaultSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT));

        FileScanTask fileScanTask = new FileScanTask(FileStatus.of("s3://bucket/f.parquet", 0, 0),
                0, Map.of(), null);
        StarRocksConnectorException exception = assertThrows(StarRocksConnectorException.class,
                () -> DeltaRowTrackingUtils.setRowCommitVersionExtendedColumn(new THdfsScanRange(),
                        fileScanTask, tupleDescriptor));

        assertTrue(exception.getMessage().contains("s3://bucket/f.parquet"));
        assertTrue(exception.getMessage().contains("defaultRowCommitVersion"));
    }

    @Test
    public void testSetRowCommitVersionExtendedSlotIds() {
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        defaultSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT));

        THdfsScanNode scanNode = new THdfsScanNode();
        DeltaRowTrackingUtils.setRowCommitVersionExtendedSlotIds(scanNode, tupleDescriptor);

        assertEquals(List.of(defaultSlot.getId().asInt()), scanNode.getExtended_slot_ids());
    }

    @Test
    public void testSetRowCommitVersionExtendedSlotIdsSkippedWhenAbsent() {
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor slot = descTable.addSlotDescriptor(tupleDescriptor);
        slot.setColumn(new Column("id", Type.BIGINT));

        THdfsScanNode scanNode = new THdfsScanNode();
        DeltaRowTrackingUtils.setRowCommitVersionExtendedSlotIds(scanNode, tupleDescriptor);

        assertFalse(scanNode.isSetExtended_slot_ids());
    }

    @Test
    public void testRewriteDeltaRowCommitVersion(@Mocked SnapshotImpl snapshot, @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));
        ColumnRefOperator rowCommitVersionRef = new ColumnRefOperator(7,
                Type.BIGINT, DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, true);
        PhysicalDeltaLakeScanOperator scanOperator = scanOperatorWithRowCommitVersion(table, rowCommitVersionRef);
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor materializedSlot = addRowCommitVersionSlot(descTable, tupleDescriptor, rowCommitVersionRef);
        Map<ColumnRefOperator, Expr> colRefToExpr = new HashMap<>();

        DeltaRowTrackingUtils.rewriteDeltaRowCommitVersion(scanOperator, descTable, tupleDescriptor, colRefToExpr);

        assertEquals("_rcv_phys", materializedSlot.getColumn().getName());
        assertEquals("_rcv_phys", materializedSlot.getColumn().getPhysicalName());
        assertTrue(materializedSlot.isMaterialized());
        SlotDescriptor defaultSlot = tupleDescriptor.getColumnSlot(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN);
        assertNotNull(defaultSlot);
        assertTrue(defaultSlot.isMaterialized());
        Expr rewritten = colRefToExpr.get(rowCommitVersionRef);
        FunctionCallExpr coalesce = assertInstanceOf(FunctionCallExpr.class, rewritten);
        assertEquals(FunctionSet.COALESCE, coalesce.getFnName().getFunction());
        assertSame(materializedSlot,
                assertInstanceOf(SlotRef.class, coalesce.getChild(0)).getSlotDescriptorWithoutCheck());
        assertSame(defaultSlot,
                assertInstanceOf(SlotRef.class, coalesce.getChild(1)).getSlotDescriptorWithoutCheck());
    }

    @Test
    public void testRewriteDeltaRowCommitVersionSkipsExistingColumn(@Mocked SnapshotImpl snapshot,
                                                                    @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));
        table.addColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, true));
        ColumnRefOperator rowCommitVersionRef = new ColumnRefOperator(7,
                Type.BIGINT, DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, true);
        PhysicalDeltaLakeScanOperator scanOperator = scanOperatorWithRowCommitVersion(table, rowCommitVersionRef);
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor userColumnSlot = addRowCommitVersionSlot(descTable, tupleDescriptor, rowCommitVersionRef);
        Map<ColumnRefOperator, Expr> colRefToExpr = new HashMap<>();

        DeltaRowTrackingUtils.rewriteDeltaRowCommitVersion(scanOperator, descTable, tupleDescriptor, colRefToExpr);

        assertEquals(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, userColumnSlot.getColumn().getName());
        assertNull(tupleDescriptor.getColumnSlot(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN));
        assertTrue(colRefToExpr.isEmpty());
    }

    @Test
    public void testRewriteDeltaRowCommitVersionRejectsDisabledRowTracking(@Mocked SnapshotImpl snapshot,
                                                                           @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata, ImmutableMap.of());
        ColumnRefOperator rowCommitVersionRef = new ColumnRefOperator(7,
                Type.BIGINT, DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, true);
        PhysicalDeltaLakeScanOperator scanOperator = scanOperatorWithRowCommitVersion(table, rowCommitVersionRef);
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        addRowCommitVersionSlot(descTable, tupleDescriptor, rowCommitVersionRef);

        UnsupportedException exception = assertThrows(UnsupportedException.class,
                () -> DeltaRowTrackingUtils.rewriteDeltaRowCommitVersion(scanOperator, descTable, tupleDescriptor,
                        new HashMap<>()));

        assertTrue(exception.getMessage().contains("requires Delta row tracking"));
    }

    @Test
    public void testRewriteDeltaRowCommitVersionRejectsMissingMaterializedColumn(@Mocked SnapshotImpl snapshot,
                                                                                 @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata, ImmutableMap.of(ENABLE_KEY, "true"));
        ColumnRefOperator rowCommitVersionRef = new ColumnRefOperator(7,
                Type.BIGINT, DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, true);
        PhysicalDeltaLakeScanOperator scanOperator = scanOperatorWithRowCommitVersion(table, rowCommitVersionRef);
        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        addRowCommitVersionSlot(descTable, tupleDescriptor, rowCommitVersionRef);

        StarRocksPlannerException exception = assertThrows(StarRocksPlannerException.class,
                () -> DeltaRowTrackingUtils.rewriteDeltaRowCommitVersion(scanOperator, descTable, tupleDescriptor,
                        new HashMap<>()));

        assertTrue(exception.getMessage().contains("has row tracking enabled but no materialized"));
    }

    @Test
    public void testRewriteDeltaRowCommitVersionRejectsMissingSlot(@Mocked SnapshotImpl snapshot,
                                                                   @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWith(snapshot, metadata,
                ImmutableMap.of(ENABLE_KEY, "true", MATERIALIZED_KEY, "_rcv_phys"));
        ColumnRefOperator rowCommitVersionRef = new ColumnRefOperator(7,
                Type.BIGINT, DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, true);
        PhysicalDeltaLakeScanOperator scanOperator = scanOperatorWithRowCommitVersion(table, rowCommitVersionRef);
        DescriptorTable descTable = new DescriptorTable();

        IllegalStateException exception = assertThrows(IllegalStateException.class,
                () -> DeltaRowTrackingUtils.rewriteDeltaRowCommitVersion(scanOperator, descTable,
                        descTable.createTupleDescriptor(), new HashMap<>()));

        assertTrue(exception.getMessage().contains("missing slot"));
    }
}
