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

package com.starrocks.planner;

import com.starrocks.analysis.TupleDescriptor;
import com.starrocks.analysis.TupleId;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Table;
import com.starrocks.common.jmockit.Deencapsulation;
import com.starrocks.connector.CatalogConnector;
import com.starrocks.connector.GetRemoteFilesParams;
import com.starrocks.connector.RemoteFileInfo;
import com.starrocks.connector.RemoteFileInfoDefaultSource;
import com.starrocks.connector.RemoteFileInfoSource;
import com.starrocks.connector.TableVersionRange;
import com.starrocks.connector.delta.DeltaConnectorScanRangeSource;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaUtils;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudConfigurationFactory;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.MetadataMgr;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.thrift.TExplainLevel;
import io.delta.kernel.Snapshot;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.actions.Format;
import io.delta.kernel.internal.actions.Metadata;
import io.delta.kernel.internal.actions.Protocol;
import mockit.Delegate;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import mockit.Mocked;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.HashMap;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Stream;

import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;

public class DeltaLakeScanNodeTest {
    @Test
    public void testInit(@Mocked GlobalStateMgr globalStateMgr,
                         @Mocked CatalogConnector connector,
                         @Mocked DeltaLakeTable table) {
        String catalog = "XXX";
        CloudConfiguration cc = CloudConfigurationFactory.buildCloudConfigurationForStorage(new HashMap<>());
        new Expectations() {
            {
                table.getCloudConfiguration();
                result = null;
                GlobalStateMgr.getCurrentState().getConnectorMgr().getConnector(catalog);
                result = connector;
                connector.getMetadata().getCloudConfiguration();
                result = cc;
                table.getCatalogName();
                result = catalog;
            }
        };
        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode = new DeltaLakeScanNode(new PlanNodeId(0), desc, "XXX", null, null, null);
    }

    @Test
    public void testNodeExplain(@Mocked GlobalStateMgr globalStateMgr, @Mocked CatalogConnector connector,
                                @Mocked DeltaLakeTable table) {
        String catalogName = "delta0";
        CloudConfiguration cloudConfiguration = CloudConfigurationFactory.
                buildCloudConfigurationForStorage(new HashMap<>());
        new Expectations() {
            {
                table.getCloudConfiguration();
                result = null;
                minTimes = 0;

                GlobalStateMgr.getCurrentState().getConnectorMgr().getConnector(catalogName);
                result = connector;
                minTimes = 0;

                connector.getMetadata().getCloudConfiguration();
                result = cloudConfiguration;
                minTimes = 0;

                table.getCatalogName();
                result = catalogName;
                minTimes = 0;

                table.getName();
                result = "table0";
                minTimes = 0;
            }
        };
        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode = new DeltaLakeScanNode(new PlanNodeId(0), desc, "Delta Scan Node", null, null, null);
        Assertions.assertFalse(scanNode.getNodeExplainString("", TExplainLevel.NORMAL).contains("partitions"));
        Assertions.assertTrue(scanNode.getNodeExplainString("", TExplainLevel.VERBOSE).contains("partitions"));
    }

    @Test
    public void testNodeExplainContainsVersion(@Mocked GlobalStateMgr globalStateMgr, @Mocked CatalogConnector connector,
                                               @Mocked DeltaLakeTable table, @Mocked Snapshot snapshot,
                                               @Mocked DeltaLakeEngine engine) {
        String catalogName = "delta0";
        CloudConfiguration cloudConfiguration = CloudConfigurationFactory.
                buildCloudConfigurationForStorage(new HashMap<>());

        new Expectations() {{
            table.getCloudConfiguration();
            result = null;
            minTimes = 0;

            GlobalStateMgr.getCurrentState().getConnectorMgr().getConnector(catalogName);
            result = connector;
            minTimes = 0;

            connector.getMetadata().getCloudConfiguration();
            result = cloudConfiguration;
            minTimes = 0;

            table.getCatalogName();
            result = catalogName;
            minTimes = 0;

            table.getName();
            result = "table0";
            minTimes = 0;

            table.getDeltaSnapshot();
            result = snapshot;
            minTimes = 0;

            table.getDeltaEngine();
            result = engine;
            minTimes = 0;

            snapshot.getVersion();
            result = 123L;
            minTimes = 0;
        }};
        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode = new DeltaLakeScanNode(new PlanNodeId(0), desc, "Delta Scan Node", null, null, null);
        String explainString = scanNode.getNodeExplainString("", TExplainLevel.NORMAL);
        assertThat(explainString, containsString("TABLE VERSION: 123"));
    }

    @Test
    public void testPrepareRetry(@Mocked GlobalStateMgr globalStateMgr,
                                 @Mocked CatalogConnector connector,
                                 @Mocked MetadataMgr metadataMgr,
                                 @Mocked DeltaLakeTable table,
                                 @Mocked SnapshotImpl snapshot,
                                 @Mocked DeltaConnectorScanRangeSource staleSource) {
        String catalogName = "delta0";
        CloudConfiguration cloudConfiguration = CloudConfigurationFactory
                .buildCloudConfigurationForStorage(new HashMap<>());

        new MockUp<DeltaUtils>() {
            @Mock
            public void checkProtocolAndMetadata(Protocol protocol, Metadata metadata) {
            }
        };

        new Expectations() {
            {
                table.getCloudConfiguration();
                result = null;
                minTimes = 0;

                GlobalStateMgr.getCurrentState().getConnectorMgr().getConnector(catalogName);
                result = connector;
                minTimes = 0;

                connector.getMetadata().getCloudConfiguration();
                result = cloudConfiguration;
                minTimes = 0;

                table.getCatalogName();
                result = catalogName;
                minTimes = 0;

                table.getDeltaSnapshot();
                result = snapshot;
                minTimes = 0;

                snapshot.getVersion();
                result = 11L;
                minTimes = 0;

                GlobalStateMgr.getCurrentState().getMetadataMgr();
                result = metadataMgr;
                minTimes = 0;

                metadataMgr.getRemoteFiles((Table) table, (GetRemoteFilesParams) any);
                result = List.of();
                minTimes = 0;
            }
        };

        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode =
                new DeltaLakeScanNode(new PlanNodeId(0), desc, "Delta Scan Node", ConstantOperator.TRUE, List.of(), null);

        Deencapsulation.setField(scanNode, "scanRangeSource", staleSource);
        scanNode.prepareRetry();

        Object rebuilt = Deencapsulation.getField(scanNode, "scanRangeSource");
        Assertions.assertNotNull(rebuilt, "prepareRetry must clear and rebuild the scan range source");
        Assertions.assertNotSame(staleSource, rebuilt, "prepareRetry must replace the stale source");
    }

    @Test
    public void testPrefersPerTableCloudConfiguration(@Mocked GlobalStateMgr globalStateMgr,
                                                      @Mocked CatalogConnector connector,
                                                      @Mocked DeltaLakeTable table) {
        String catalogName = "delta0";
        CloudConfiguration perTableCloudConfiguration = CloudConfigurationFactory
                .buildCloudConfigurationForStorage(new HashMap<>());
        new Expectations() {
            {
                table.getCatalogName();
                result = catalogName;
                minTimes = 0;

                table.getCloudConfiguration();
                result = perTableCloudConfiguration;
                minTimes = 0;

                // times = 0 must bind to the chain root (getMetadata), not the trailing call.
                connector.getMetadata();
                times = 0;
            }
        };
        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode = new DeltaLakeScanNode(new PlanNodeId(0), desc, "Delta Scan Node", null, null, null);
        Assertions.assertNotNull(scanNode);
    }

    private static Stream<Arguments> scanRangeSourceVersionCases() {
        return Stream.of(
                Arguments.of("sync null range", false, null, 11L),
                Arguments.of("sync empty range", false, TableVersionRange.empty(), 11L),
                Arguments.of("sync pinned range", false, TableVersionRange.withEnd(Optional.of(3L)), 3L),
                Arguments.of("incremental empty range", true, TableVersionRange.empty(), 11L),
                Arguments.of("incremental pinned range", true, TableVersionRange.withEnd(Optional.of(3L)), 3L));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("scanRangeSourceVersionCases")
    public void testSetupScanRangeSourcePassesExpectedVersion(String name, boolean incremental,
                                                              TableVersionRange requestedRange, long expectedVersion,
                                                              @Mocked GlobalStateMgr globalStateMgr,
                                                              @Mocked CatalogConnector connector,
                                                              @Mocked MetadataMgr metadataMgr,
                                                              @Mocked DeltaLakeTable table,
                                                              @Mocked SnapshotImpl snapshot,
                                                              @Mocked Metadata metadata,
                                                              @Mocked Format format) throws Exception {
        String catalogName = "delta0";
        CloudConfiguration cloudConfiguration = CloudConfigurationFactory
                .buildCloudConfigurationForStorage(new HashMap<>());
        AtomicReference<GetRemoteFilesParams> capturedParams = new AtomicReference<>();
        AtomicReference<Boolean> capturedIncremental = new AtomicReference<>();
        ConstantOperator predicate = ConstantOperator.TRUE;
        List<String> fieldNames = List.of("id", "payload");

        new MockUp<DeltaUtils>() {
            @Mock
            public void checkProtocolAndMetadata(Protocol protocol, Metadata metadata) {
            }
        };

        new Expectations() {
            {
                table.getCloudConfiguration();
                result = null;
                minTimes = 0;

                GlobalStateMgr.getCurrentState().getConnectorMgr().getConnector(catalogName);
                result = connector;
                minTimes = 0;

                connector.getMetadata().getCloudConfiguration();
                result = cloudConfiguration;
                minTimes = 0;

                table.getCatalogName();
                result = catalogName;
                minTimes = 0;

                table.getDeltaSnapshot();
                result = snapshot;
                minTimes = 0;

                snapshot.getProtocol();
                result = null;
                minTimes = 0;

                snapshot.getMetadata();
                result = null;
                minTimes = 0;

                snapshot.getVersion();
                result = 11L;
                minTimes = 0;

                table.getDeltaMetadata();
                result = metadata;
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
                        capturedIncremental.set(false);
                        capturedParams.set(params);
                        return List.of();
                    }
                };
                minTimes = 0;

                metadataMgr.getRemoteFilesAsync((Table) table, (GetRemoteFilesParams) any);
                result = new Delegate<RemoteFileInfoSource>() {
                    RemoteFileInfoSource getRemoteFilesAsync(Table requestedTable, GetRemoteFilesParams params) {
                        capturedIncremental.set(true);
                        capturedParams.set(params);
                        return RemoteFileInfoDefaultSource.EMPTY;
                    }
                };
                minTimes = 0;
            }
        };

        TupleDescriptor desc = new TupleDescriptor(new TupleId(0));
        desc.setTable(table);
        DeltaLakeScanNode scanNode =
                new DeltaLakeScanNode(new PlanNodeId(0), desc, "Delta Scan Node", predicate, fieldNames, null);
        scanNode.setTableVersionRange(requestedRange);
        scanNode.setupScanRangeSource(incremental);

        Assertions.assertNotNull(capturedParams.get());
        Assertions.assertEquals(incremental, capturedIncremental.get());
        Assertions.assertEquals(Optional.of(expectedVersion), capturedParams.get().getTableVersionRange().end());
        Assertions.assertSame(predicate, capturedParams.get().getPredicate());
        Assertions.assertEquals(fieldNames, capturedParams.get().getFieldNames());
        Assertions.assertNotNull(Deencapsulation.getField(scanNode, "scanRangeSource"));
    }
}
