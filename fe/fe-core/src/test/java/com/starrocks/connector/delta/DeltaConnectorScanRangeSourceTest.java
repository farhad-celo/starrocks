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

import com.starrocks.analysis.DescriptorTable;
import com.starrocks.analysis.SlotDescriptor;
import com.starrocks.analysis.TupleDescriptor;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.Type;
import com.starrocks.connector.RemoteFileInfoDefaultSource;
import com.starrocks.planner.PartitionIdGenerator;
import com.starrocks.thrift.TExpr;
import com.starrocks.thrift.TExprNodeType;
import com.starrocks.thrift.THdfsPartition;
import com.starrocks.thrift.THdfsScanRange;
import com.starrocks.thrift.TScanRangeLocations;
import io.delta.kernel.internal.actions.Format;
import io.delta.kernel.internal.actions.Metadata;
import io.delta.kernel.utils.FileStatus;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.mockito.Mockito;

import java.util.List;
import java.util.Map;

public class DeltaConnectorScanRangeSourceTest {
    @Test
    public void testDefaultRowCommitVersionIsSerializedAsExtendedColumn() {
        Format format = Mockito.mock(Format.class);
        Mockito.when(format.getProvider()).thenReturn("parquet");

        Metadata metadata = Mockito.mock(Metadata.class);
        Mockito.when(metadata.getFormat()).thenReturn(format);

        DeltaLakeTable table = Mockito.mock(DeltaLakeTable.class);
        Mockito.when(table.getDeltaMetadata()).thenReturn(metadata);
        Mockito.when(table.getType()).thenReturn(Table.TableType.DELTALAKE);
        Mockito.when(table.getPartitionColumnNames()).thenReturn(List.of());
        Mockito.when(table.getPartitionColumns()).thenReturn(List.of());
        Mockito.when(table.getTableLocation()).thenReturn("/tmp/table");
        Mockito.when(table.getId()).thenReturn(1L);
        Mockito.when(table.toHdfsPartition(Mockito.any())).thenReturn(new THdfsPartition());

        DescriptorTable descTable = new DescriptorTable();
        TupleDescriptor tupleDescriptor = descTable.createTupleDescriptor();
        SlotDescriptor defaultSlot = descTable.addSlotDescriptor(tupleDescriptor);
        defaultSlot.setColumn(new Column(DeltaLakeTable.ROW_COMMIT_VERSION_DEFAULT_COLUMN, Type.BIGINT));

        FileScanTask fileScanTask = new FileScanTask(
                FileStatus.of("/tmp/table/part-00000.snappy.parquet", 100L, 1L), 10L, Map.of(), null, 42L);
        RemoteFileInfoDefaultSource source = new RemoteFileInfoDefaultSource(List.of(
                new DeltaRemoteFileInfo(fileScanTask)));
        DeltaConnectorScanRangeSource scanRangeSource =
                new DeltaConnectorScanRangeSource(table, source, PartitionIdGenerator.of(), tupleDescriptor);

        List<TScanRangeLocations> outputs = scanRangeSource.getSourceOutputs(10);

        Assertions.assertEquals(1, outputs.size());
        THdfsScanRange hdfsScanRange = outputs.get(0).getScan_range().getHdfs_scan_range();
        TExpr defaultExpr = hdfsScanRange.getExtended_columns().get(defaultSlot.getId().asInt());
        Assertions.assertNotNull(defaultExpr);
        Assertions.assertEquals(TExprNodeType.INT_LITERAL, defaultExpr.nodes.get(0).node_type);
        Assertions.assertEquals(42L, defaultExpr.nodes.get(0).int_literal.value);
    }
}
