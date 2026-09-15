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

import com.google.common.base.Preconditions;
import com.google.common.collect.Lists;
import com.starrocks.analysis.DescriptorTable;
import com.starrocks.analysis.Expr;
import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.LiteralExpr;
import com.starrocks.analysis.SlotDescriptor;
import com.starrocks.analysis.SlotId;
import com.starrocks.analysis.SlotRef;
import com.starrocks.analysis.TupleDescriptor;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Function;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.Type;
import com.starrocks.common.AnalysisException;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.sql.common.StarRocksPlannerException;
import com.starrocks.sql.optimizer.operator.physical.PhysicalDeltaLakeScanOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.thrift.TExpr;
import com.starrocks.thrift.THdfsScanNode;
import com.starrocks.thrift.THdfsScanRange;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.starrocks.sql.common.ErrorType.INTERNAL_ERROR;
import static com.starrocks.sql.common.UnsupportedException.unsupportedException;

public final class DeltaRowTrackingUtils {
    private DeltaRowTrackingUtils() {
    }

    public static boolean isRowCommitVersionColumn(String columnName) {
        return DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN.equalsIgnoreCase(columnName);
    }

    public static List<Column> getDeltaRowTrackingColumns(Table table) {
        if (!(table instanceof DeltaLakeTable)) {
            return List.of();
        }
        if (table.getColumn(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN) != null) {
            return List.of();
        }
        return List.of(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN, Type.BIGINT, false, null, true,
                null, ""));
    }

    public static void setRowCommitVersionExtendedColumn(THdfsScanRange hdfsScanRange, FileScanTask fileScanTask,
                                                         TupleDescriptor desc) {
        if (desc == null) {
            return;
        }
        SlotDescriptor defaultSlot = desc.getColumnSlot(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN);
        if (defaultSlot == null) {
            return;
        }
        Long defaultVersion = fileScanTask.getDefaultRowCommitVersion();
        if (defaultVersion == null) {
            throw new StarRocksConnectorException("Delta file " + fileScanTask.getFileStatus().getPath() +
                    " is missing row-tracking defaultRowCommitVersion required by " +
                    DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN);
        }
        try {
            LiteralExpr value = LiteralExpr.create(String.valueOf(defaultVersion), Type.BIGINT);
            Map<Integer, TExpr> extendedColumns = hdfsScanRange.isSetExtended_columns()
                    ? new HashMap<>(hdfsScanRange.getExtended_columns())
                    : new HashMap<>();
            extendedColumns.put(defaultSlot.getId().asInt(), value.treeToThrift());
            hdfsScanRange.setExtended_columns(extendedColumns);
        } catch (AnalysisException e) {
            throw new StarRocksConnectorException("failed to build " +
                    DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN + " default value", e);
        }
    }

    public static void setRowCommitVersionExtendedSlotIds(THdfsScanNode hdfsScanNode, TupleDescriptor desc) {
        List<Integer> extendedSlotIds = new ArrayList<>();
        for (SlotDescriptor slot : desc.getSlots()) {
            Column column = slot.getColumn();
            if (column != null
                    && DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN.equalsIgnoreCase(column.getName())) {
                extendedSlotIds.add(slot.getId().asInt());
            }
        }
        if (!extendedSlotIds.isEmpty()) {
            hdfsScanNode.setExtended_slot_ids(extendedSlotIds);
        }
    }

    // Rewrite _row_commit_version to COALESCE(materialized parquet column, per-file default). The
    // default alone is wrong for OPTIMIZE-compacted files, which carry a per-row materialized value.
    public static void rewriteDeltaRowCommitVersion(PhysicalDeltaLakeScanOperator node, DescriptorTable descTable,
                                                    TupleDescriptor tupleDescriptor,
                                                    Map<ColumnRefOperator, Expr> colRefToExpr) {
        DeltaLakeTable table = (DeltaLakeTable) node.getTable();
        if (table.getColumn(DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN) != null) {
            return;
        }

        ColumnRefOperator rcvRef = null;
        for (Map.Entry<ColumnRefOperator, Column> entry : node.getColRefToColumnMetaMap().entrySet()) {
            if (isRowCommitVersionColumn(entry.getKey().getName())) {
                rcvRef = entry.getKey();
                break;
            }
        }
        if (rcvRef == null) {
            return;
        }

        if (!table.isRowTrackingEnabled()) {
            throw unsupportedException(
                    "Column " + DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN + " requires Delta row tracking " +
                            "(delta.enableRowTracking=true) on table " + table.getName());
        }
        String materializedName = table.getMaterializedRowCommitVersionColumnName();
        if (materializedName == null) {
            throw new StarRocksPlannerException(
                    "Delta table " + table.getName() + " has row tracking enabled but no materialized " +
                            DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN + " column name", INTERNAL_ERROR);
        }

        // Repurpose the hidden column's slot into the materialized parquet read (resolved by physical name).
        SlotDescriptor materializedSlot = descTable.getSlotDesc(new SlotId(rcvRef.getId()));
        Preconditions.checkState(materializedSlot != null, "missing slot for %s",
                DeltaLakeTable.ROW_COMMIT_VERSION_COLUMN);
        Column materializedColumn = new Column(materializedName, Type.BIGINT, false, null, null, true, null, "",
                Column.COLUMN_UNIQUE_ID_INIT_VALUE, materializedName);
        materializedSlot.setColumn(materializedColumn);
        materializedSlot.setType(Type.BIGINT);
        materializedSlot.setIsNullable(true);
        materializedSlot.setIsMaterialized(true);

        // Backing slot for the per-file default constant.
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        Column defaultColumn = new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT, false,
                null, true, null, "");
        defaultSlot.setColumn(defaultColumn);
        defaultSlot.setType(Type.BIGINT);
        defaultSlot.setIsNullable(true);
        defaultSlot.setIsMaterialized(true);

        List<Expr> args = Lists.newArrayList(
                new SlotRef(materializedName, materializedSlot),
                new SlotRef(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, defaultSlot));
        FunctionCallExpr coalesce = new FunctionCallExpr(FunctionSet.COALESCE, args);
        coalesce.setFn(Expr.getBuiltinFunction(FunctionSet.COALESCE, new Type[] {Type.BIGINT, Type.BIGINT},
                Function.CompareMode.IS_NONSTRICT_SUPERTYPE_OF));
        coalesce.setType(Type.BIGINT);
        colRefToExpr.put(rcvRef, coalesce);
    }
}
