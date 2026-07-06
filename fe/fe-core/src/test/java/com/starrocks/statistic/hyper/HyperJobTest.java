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

package com.starrocks.statistic.hyper;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.catalog.ArrayType;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.Database;
import com.starrocks.catalog.OlapTable;
import com.starrocks.catalog.Partition;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.Type;
import com.starrocks.common.Config;
import com.starrocks.common.FeConstants;
import com.starrocks.common.Pair;
import com.starrocks.qe.ConnectContext;
import com.starrocks.qe.StmtExecutor;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.DistributedEnvPlanTestBase;
import com.starrocks.sql.plan.PlanTestBase;
import com.starrocks.statistic.AnalyzeStatus;
import com.starrocks.statistic.HyperStatisticsCollectJob;
import com.starrocks.statistic.NativeAnalyzeStatus;
import com.starrocks.statistic.StatisticUtils;
import com.starrocks.statistic.StatsConstants;
import com.starrocks.statistic.base.CollectionTypeColumnStats;
import com.starrocks.statistic.base.ColumnClassifier;
import com.starrocks.statistic.base.ColumnStats;
import com.starrocks.statistic.base.PartitionSampler;
import com.starrocks.statistic.base.PrimitiveTypeColumnStats;
import com.starrocks.statistic.base.SubFieldColumnStats;
import com.starrocks.statistic.base.TabletSampler;
import com.starrocks.statistic.sample.SampleInfo;
import com.starrocks.statistic.sample.TabletStats;
import com.starrocks.utframe.StarRocksAssert;
import mockit.Mock;
import mockit.MockUp;
import org.apache.velocity.VelocityContext;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class HyperJobTest extends DistributedEnvPlanTestBase {

    private static Database db;

    private static Table table;

    private static Table wideTable;

    private static PartitionSampler sampler;

    private static PartitionSampler wideSampler;

    private static long pid;

    private static long widePid;

    @BeforeAll
    public static void beforeClass() throws Exception {
        PlanTestBase.beforeClass();
        StarRocksAssert starRocksAssert = new StarRocksAssert(connectContext);
        FeConstants.runningUnitTest = true;
        starRocksAssert.withTable("create table t_struct(c0 INT, " +
                "c1 date," +
                "c2 varchar(255)," +
                "c3 decimal(10, 2)," +
                "c4 struct<a int, b array<struct<a int, b int>>>," +
                "c5 struct<a int, b int>," +
                "c6 struct<a int, b int, c struct<a int, b int>, d array<int>>," +
                "c7 array<int>) " +
                "duplicate key(c0) distributed by hash(c0) buckets 1 " +
                "properties('replication_num'='1');");
        starRocksAssert.withTable("create table t_wide_stats(k1 INT, " +
                "c_short varchar(100)," +
                "c_boundary varchar(512)," +
                "c_wide varchar(2000)," +
                "c_wide_a varchar(1500)," +
                "c_wide_b varchar(3000)," +
                "c_arr_wide array<varchar(2000)>," +
                "c_bigint bigint) " +
                "duplicate key(k1) distributed by hash(k1) buckets 1 " +
                "properties('replication_num'='1');");
        db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        table = GlobalStateMgr.getCurrentState().getLocalMetastore().getTable("test", "t_struct");
        pid = table.getPartition("t_struct").getId();
        sampler = PartitionSampler.create(table, List.of(pid), Maps.newHashMap(), null);
        wideTable = GlobalStateMgr.getCurrentState().getLocalMetastore().getTable("test", "t_wide_stats");
        widePid = wideTable.getPartition("t_wide_stats").getId();
        wideSampler = PartitionSampler.create(wideTable, List.of(widePid), Maps.newHashMap(), null);

        for (Partition partition : ((OlapTable) table).getAllPartitions()) {
            partition.getDefaultPhysicalPartition().getBaseIndex().setRowCount(10000);
        }
        for (Partition partition : ((OlapTable) wideTable).getAllPartitions()) {
            partition.getDefaultPhysicalPartition().getBaseIndex().setRowCount(10000);
        }
    }

    @Test
    public void generateComplexTypeColumnTask() throws Exception {
        List<String> columnNames = table.getColumns().stream().map(Column::getName).collect(Collectors.toList());
        List<Type> columnTypes = table.getColumns().stream().map(c -> c.getType()).collect(Collectors.toList());

        List<HyperQueryJob> jobs =
                HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, columnNames, columnTypes, List.of(pid), 1,
                        false, Map.of());
        Assertions.assertEquals(2, jobs.size());
        assertInstanceOf(ConstQueryJob.class, jobs.get(1));
        for (final var job : jobs) {
            List<String> sqls = job.buildQuerySQL();
            for (String sql : sqls) {
                Assertions.assertNotNull(getFragmentPlan(sql)); // Make sure the query can be executed.
            }
        }
    }

    @Test
    public void generatePrimitiveTypeColumnTask() {
        List<String> columnNames = table.getColumns().stream().map(Column::getName).collect(Collectors.toList());
        List<Type> columnTypes = table.getColumns().stream().map(c -> c.getType()).collect(Collectors.toList());

        ColumnClassifier cc = ColumnClassifier.of(columnNames, columnTypes, table, false, Map.of());
        ColumnStats columnStat = cc.getColumnStats().stream().filter(c -> c instanceof PrimitiveTypeColumnStats)
                .findAny().orElse(null);

        VelocityContext context = HyperStatisticSQLs.buildBaseContext(db, table, table.getPartition(pid), columnStat);
        context.put("dataSize", columnStat.getFullDataSize());
        context.put("countNullFunction", columnStat.getFullNullCount());
        context.put("hllFunction", columnStat.getNDV());
        context.put("maxFunction", columnStat.getMax());
        context.put("minFunction", columnStat.getMin());
        context.put("fromTable", "`" + db.getOriginName() + "`.`" + table.getName() +
                "` partition `" + table.getPartition(pid).getName() + "`");
        context.put("laterals", columnStat.getLateralJoin());
        context.put("collectionSizeFunction", columnStat.getCollectionSize());
        String sql = HyperStatisticSQLs.build(context, HyperStatisticSQLs.BATCH_FULL_STATISTIC_TEMPLATE);
        assertContains(sql, "hex(hll_serialize(IFNULL(hll_raw(`c0`)");
        List<StatementBase> stmt = SqlParser.parse(sql, connectContext.getSessionVariable());
        assertInstanceOf(QueryStatement.class, stmt.get(0));
        assertDoesNotThrow(() -> getFragmentPlan(sql));
    }

    @Test
    public void generateSubFieldTypeColumnTask() {
        List<String> columnNames = Lists.newArrayList("c1", "c4.b", "c6.c.b");
        List<Type> columnTypes = Lists.newArrayList(Type.DATE, new ArrayType(Type.ANY_STRUCT), Type.INT);

        ColumnClassifier cc = ColumnClassifier.of(columnNames, columnTypes, table, false, Map.of());
        List<ColumnStats> columnStat = cc.getColumnStats().stream().filter(c -> c instanceof SubFieldColumnStats)
                .collect(Collectors.toList());
        String sql = HyperStatisticSQLs.buildSampleSQL(db, table, table.getPartition(pid), columnStat, sampler,
                HyperStatisticSQLs.BATCH_SAMPLE_STATISTIC_SELECT_TEMPLATE);
        Assertions.assertEquals(2, columnStat.size());
        List<StatementBase> stmt = SqlParser.parse(sql, connectContext.getSessionVariable());
        assertInstanceOf(QueryStatement.class, stmt.get(0));
        assertDoesNotThrow(() -> getFragmentPlan(sql));
    }

    public Pair<List<String>, List<Type>> initColumn(List<String> cols) {
        List<String> columnNames = Lists.newArrayList();
        List<Type> columnTypes = Lists.newArrayList();
        for (String col : cols) {
            Column c = table.getColumn(col);
            columnNames.add(c.getName());
            columnTypes.add(c.getType());
        }
        return Pair.create(columnNames, columnTypes);
    }

    private Pair<List<String>, List<Type>> initWideColumn(List<String> cols) {
        List<String> columnNames = Lists.newArrayList();
        List<Type> columnTypes = Lists.newArrayList();
        for (String col : cols) {
            Column c = wideTable.getColumn(col);
            columnNames.add(c.getName());
            columnTypes.add(c.getType());
        }
        return Pair.create(columnNames, columnTypes);
    }

    private SampleQueryJob getSampleQueryJob(List<String> cols) {
        Pair<List<String>, List<Type>> pair = initWideColumn(cols);
        return HyperQueryJob.createSampleQueryJobs(1L, connectContext, db, wideTable, pair.first,
                        pair.second, List.of(widePid), 1, wideSampler, false).stream()
                .filter(SampleQueryJob.class::isInstance)
                .map(SampleQueryJob.class::cast)
                .findFirst()
                .orElseThrow(() -> new AssertionError("missing SampleQueryJob"));
    }

    private FullQueryJob getFullQueryJob(List<String> cols) {
        Pair<List<String>, List<Type>> pair = initWideColumn(cols);
        return HyperQueryJob.createFullQueryJobs(1L, connectContext, db, wideTable, pair.first,
                        pair.second, List.of(widePid), 1, false).stream()
                .filter(FullQueryJob.class::isInstance)
                .map(FullQueryJob.class::cast)
                .findFirst()
                .orElseThrow(() -> new AssertionError("missing FullQueryJob"));
    }

    @Test
    public void testConstQueryJobs() {
        new MockUp<StmtExecutor>() {
            @Mock
            public void execute() throws Exception {
            }
        };
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c4", "c5", "c6"));

        HyperStatisticsCollectJob job = new HyperStatisticsCollectJob(db, table, List.of(pid), pair.first, pair.second,
                StatsConstants.AnalyzeType.FULL,
                StatsConstants.ScheduleType.ONCE, Maps.newHashMap(), false);

        ConnectContext context = StatisticUtils.buildConnectContext();
        AnalyzeStatus status = new NativeAnalyzeStatus(1, 1, 1, pair.first, StatsConstants.AnalyzeType.FULL,
                StatsConstants.ScheduleType.ONCE, Maps.newHashMap(), LocalDateTime.now());
        try {
            job.collect(context, status);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Test
    public void testConstQueryJobSkipsPartitionWithZeroRowCount() {
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c4", "c5", "c6"));
        for (Partition partition : ((OlapTable) table).getAllPartitions()) {
            partition.getDefaultPhysicalPartition().getBaseIndex().setRowCount(0);
        }
        try {
            List<HyperQueryJob> jobs = HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, pair.first,
                    pair.second, List.of(pid), 1, false);
            ConstQueryJob constQueryJob = jobs.stream()
                    .filter(ConstQueryJob.class::isInstance)
                    .map(ConstQueryJob.class::cast)
                    .findFirst()
                    .orElseThrow(() -> new AssertionError("missing ConstQueryJob"));

            constQueryJob.queryStatistics();
            Assertions.assertTrue(constQueryJob.getStatisticsValueSQL().isEmpty());
            Assertions.assertTrue(constQueryJob.getStatisticsData().isEmpty());
        } finally {
            for (Partition partition : ((OlapTable) table).getAllPartitions()) {
                partition.getDefaultPhysicalPartition().getBaseIndex().setRowCount(10000);
            }
        }
    }

    @Test
    public void testFullJobs() {
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c1", "c2", "c3"));

        List<HyperQueryJob> jobs = HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, pair.first,
                pair.second, List.of(pid), 1, false, Map.of());

        Assertions.assertEquals(1, jobs.size());

        List<String> sqls = jobs.get(0).buildQuerySQL();
        Assertions.assertEquals(3, sqls.size());

        assertContains(sqls.get(0), "hex(hll_serialize(IFNULL(hll_raw(`c1`),");
        assertContains(sqls.get(1), "FROM `test`.`t_struct` partition `t_struct`");

        for (final var sql : sqls) {
            assertDoesNotThrow(() -> getFragmentPlan(sql));
        }
    }

    @Test
    public void testSampleJobs() {
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c1", "c2", "c3"));
        final var tabletId = getAnyTabletId(table);
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(tabletId, pid, 5000000));
            }
        };

        List<HyperQueryJob> jobs = HyperQueryJob.createSampleQueryJobs(1L, connectContext, db, table, pair.first,
                pair.second, List.of(pid), 1, sampler, false, Map.of());

        Assertions.assertEquals(2, jobs.size());
        assertInstanceOf(MetaQueryJob.class, jobs.get(0));
        assertInstanceOf(SampleQueryJob.class, jobs.get(1));

        List<String> sqls = jobs.get(1).buildQuerySQL();
        Assertions.assertEquals(1, sqls.size());

        assertContains(sqls.get(0), "with base_cte_table as ( SELECT * FROM (SELECT * FROM `test`.`t_struct` " +
                "TABLET(" + tabletId + ") SAMPLE('percent'='10')) t_medium_high)");
        assertContains(sqls.get(0), "cast(IFNULL(SUM(CHAR_LENGTH(`c2`)) * 0/ COUNT(*), 0) as BIGINT), " +
                "hex(hll_serialize(IFNULL(hll_raw(`c2`), hll_empty())))," +
                " cast((COUNT(*) - COUNT(`c2`)) * 0 / COUNT(*) as BIGINT), " +
                "IFNULL(MAX(LEFT(`c2`, 200)), ''), IFNULL(MIN(LEFT(`c2`, 200)), ''), cast(-1.0 as BIGINT) " +
                "FROM (SELECT * FROM base_cte_table ) from_cte ");

        for (final var sql : sqls) {
            assertDoesNotThrow(() -> getFragmentPlan(sql));
        }
    }

    @Test
    public void testArrayNDV() {
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c7"));

        List<HyperQueryJob> jobs = HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, pair.first,
                pair.second, List.of(pid), 1, true, Map.of());

        Assertions.assertEquals(1, jobs.size());

        List<String> sqls = jobs.get(0).buildQuerySQL();
        assertContains(sqls.get(0), "'00'");
        Config.enable_manual_collect_array_ndv = true;
        sqls = jobs.get(0).buildQuerySQL();
        assertContains(sqls.get(0), "hex(hll_serialize(IFNULL(hll_raw(crc32_hash(`c7`)), hll_empty())))");
        Config.enable_manual_collect_array_ndv = false;

        for (final var sql : sqls) {
            assertDoesNotThrow(() -> getFragmentPlan(sql));
        }
    }

    @Test
    public void testSubfieldSampleJobs() {
        List<String> columnNames = Lists.newArrayList("c4.b", "c6.c.b");
        List<Type> columnTypes = Lists.newArrayList(new ArrayType(Type.ANY_STRUCT), Type.INT);
        final var tabletId = getAnyTabletId(table);

        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(tabletId, pid, 5000000));
            }
        };

        List<HyperQueryJob> jobs = HyperQueryJob.createSampleQueryJobs(1L, connectContext, db, table, columnNames,
                columnTypes, List.of(pid), 1, sampler, false, Map.of());

        Assertions.assertEquals(1, jobs.size());
        assertInstanceOf(SampleQueryJob.class, jobs.get(0));

        List<String> sqls = jobs.get(0).buildQuerySQL();
        Assertions.assertEquals(2, sqls.size());

        assertContains(sqls.get(1),
                "with base_cte_table as ( SELECT * FROM (SELECT * FROM `test`.`t_struct` TABLET(" + tabletId + ") SAMPLE" +
                        "('percent'='10'))");
        assertContains(sqls.get(1), "'c6.c.b', cast(0 as BIGINT), cast(4 * 0 as BIGINT), ");
        assertContains(sqls.get(1), "hex(hll_serialize(IFNULL(hll_raw(`c6`.`c`.`b`), hll_empty()))), ");
        assertContains(sqls.get(1), "cast((COUNT(*) - COUNT(`c6`.`c`.`b`)) * 0 / COUNT(*) as BIGINT), " +
                "IFNULL(MAX(`c6`.`c`.`b`), ''), IFNULL(MIN(`c6`.`c`.`b`), ''), cast(-1.0 as BIGINT) " +
                "FROM (SELECT * FROM base_cte_table ) from_cte");

        for (final var sql : sqls) {
            assertDoesNotThrow(() -> getFragmentPlan(sql));
        }
    }

    @Test
    public void testSampleRows() {
        new MockUp<TabletSampler>() {
            @Mock
            public List<TabletStats> sample() {
                return List.of(new TabletStats(1, pid, 5000000));
            }

        };
        PartitionSampler sampler = PartitionSampler.create(table, List.of(pid), Maps.newHashMap(), null);
        Assertions.assertEquals(5550000, sampler.getSampleInfo(pid).getSampleRowCount());
    }

    @Test
    public void testHyperQueryJobContextStartTime() {
        Pair<List<String>, List<Type>> pair = initColumn(List.of("c1", "c2", "c3"));

        final var tabletId = getAnyTabletId(table);

        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(tabletId, pid, 5000000));
            }
        };

        List<HyperQueryJob> jobs = HyperQueryJob.createSampleQueryJobs(1L, connectContext, db, table, pair.first,
                pair.second, List.of(pid), 1, sampler, false, Map.of());
        long startTime = connectContext.getStartTime();
        for (HyperQueryJob job : jobs) {
            job.queryStatistics();
            Assertions.assertNotEquals(startTime, connectContext.getStartTime());
        }
    }

    @Test
    public void testKillAnalyzeCancelHyperQueryJobEarly() {
        long analyzeId = 12345L;

        ConnectContext statsCtx = StatisticUtils.buildConnectContext();
        GlobalStateMgr.getCurrentState().getAnalyzeMgr().registerConnection(analyzeId, statsCtx);

        try {
            GlobalStateMgr.getCurrentState().getAnalyzeMgr().killConnection(analyzeId);

            Pair<List<String>, List<Type>> pair = initColumn(List.of("c1", "c2", "c3"));
            List<HyperQueryJob> jobs = HyperQueryJob.createFullQueryJobs(
                    analyzeId, statsCtx, db, table, pair.first, pair.second, List.of(pid), 1, false, Map.of());

            RuntimeException ex = Assertions.assertThrows(RuntimeException.class, () -> jobs.get(0).queryStatistics());
            Assertions.assertTrue(ex.getMessage().contains("USER_CANCEL"), ex.getMessage());
        } finally {
            GlobalStateMgr.getCurrentState().getAnalyzeMgr().unregisterConnection(analyzeId, false);
        }
    }

    @Test
    public void testSampleJobsWideStringColumnSplit() {
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        SampleQueryJob job = getSampleQueryJob(List.of("c_short", "c_wide"));

        long original = Config.statistics_large_string_column_merge_threshold;
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            List<String> sqls = job.buildQuerySQL();
            // Expect 2 SQLs: 1 for the wide column alone, 1 for the narrow column.
            Assertions.assertEquals(2, sqls.size());

            String wideSql = sqls.stream().filter(s -> s.contains("'c_wide'")).findFirst()
                    .orElseThrow(() -> new AssertionError("missing SQL for wide column"));
            Assertions.assertFalse(wideSql.contains("'c_short'"),
                    "wide-column SQL must not contain c_short: " + wideSql);
            List<StatementBase> wideStmt = SqlParser.parse(wideSql, connectContext.getSessionVariable());
            Assertions.assertTrue(wideStmt.get(0) instanceof QueryStatement);

            String narrowSql = sqls.stream().filter(s -> s.contains("'c_short'")).findFirst()
                    .orElseThrow(() -> new AssertionError("missing SQL for narrow column"));
            Assertions.assertFalse(narrowSql.contains("'c_wide'"),
                    "narrow-column SQL must not contain c_wide: " + narrowSql);
            List<StatementBase> narrowStmt = SqlParser.parse(narrowSql, connectContext.getSessionVariable());
            Assertions.assertTrue(narrowStmt.get(0) instanceof QueryStatement);
        } finally {
            Config.statistics_large_string_column_merge_threshold = original;
        }
    }

    @Test
    public void testSampleJobsMultipleWideColumnsEachOwnSql() {
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        SampleQueryJob job = getSampleQueryJob(List.of("c_short", "c_wide_a", "c_wide_b"));

        long original = Config.statistics_large_string_column_merge_threshold;
        int origParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            // Bump parallelism to 3 so the narrow column wouldn't be split by partition() alone.
            connectContext.getSessionVariable().setStatisticCollectParallelism(3);

            List<String> sqls = job.buildQuerySQL();
            // 2 SQLs for the wide columns (one each) + 1 SQL for the narrow column = 3 total.
            Assertions.assertEquals(3, sqls.size(),
                    "wide columns each own SQL, narrow ones batched: " + sqls);

            // Each wide column appears in exactly one SQL and that SQL contains no other column.
            long wideASqls = sqls.stream().filter(s -> s.contains("'c_wide_a'")).count();
            long wideBSqls = sqls.stream().filter(s -> s.contains("'c_wide_b'")).count();
            Assertions.assertEquals(1, wideASqls, "c_wide_a should appear in exactly 1 SQL");
            Assertions.assertEquals(1, wideBSqls, "c_wide_b should appear in exactly 1 SQL");

            // The two wide-column SQLs must be different from each other (each isolated).
            String wideASql = sqls.stream().filter(s -> s.contains("'c_wide_a'")).findFirst().get();
            String wideBSql = sqls.stream().filter(s -> s.contains("'c_wide_b'")).findFirst().get();
            Assertions.assertFalse(wideASql.contains("'c_wide_b'"),
                    "wide column SQLs must not share a SQL");
            Assertions.assertFalse(wideBSql.contains("'c_wide_a'"),
                    "wide column SQLs must not share a SQL");
            Assertions.assertFalse(wideASql.contains("'c_short'"),
                    "wide column SQL must not contain narrow column");

            // The narrow column should land in its own batched SQL.
            String narrowSql = sqls.stream().filter(s -> s.contains("'c_short'")).findFirst()
                    .orElseThrow(() -> new AssertionError("missing SQL for narrow column"));
            Assertions.assertFalse(narrowSql.contains("'c_wide_a'"));
            Assertions.assertFalse(narrowSql.contains("'c_wide_b'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = original;
            connectContext.getSessionVariable().setStatisticCollectParallelism(origParallel);
        }
    }

    @Test
    public void testSampleJobsWideColumnThresholdDisabled() {
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        SampleQueryJob job = getSampleQueryJob(List.of("c_short", "c_wide"));

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 0;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            // Both columns share the same SQL (no wide-column isolation).
            Assertions.assertEquals(1, sqls.size());
            Assertions.assertTrue(sqls.get(0).contains("'c_short'"));
            Assertions.assertTrue(sqls.get(0).contains("'c_wide'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testSampleJobsWideColumnThresholdBoundaryInclusive() {
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        SampleQueryJob job = getSampleQueryJob(List.of("c_short", "c_boundary"));

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 512;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            Assertions.assertEquals(1, sqls.size(),
                    "boundary length == threshold must batch with narrow, not isolate");
            Assertions.assertTrue(sqls.get(0).contains("'c_short'"));
            Assertions.assertTrue(sqls.get(0).contains("'c_boundary'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testSampleJobsAllWideNoNormalBatch() {
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        SampleQueryJob job = getSampleQueryJob(List.of("c_wide_a", "c_wide_b"));

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            List<String> sqls = job.buildQuerySQL();
            Assertions.assertEquals(2, sqls.size(),
                    "two wide columns with no narrow column must produce exactly 2 isolated SQLs");
            for (String sql : sqls) {
                boolean hasA = sql.contains("'c_wide_a'");
                boolean hasB = sql.contains("'c_wide_b'");
                Assertions.assertTrue(hasA ^ hasB,
                        "each SQL must contain exactly one wide column, never both: " + sql);
            }
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
        }
    }

    @Test
    public void testSampleJobsArrayColumnFallsThroughToNormal() {
        // ARRAY columns with wide element types are not isolated because only top-level
        // char-family columns are classified as wide.
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        List<ColumnStats> stats = List.of(
                new PrimitiveTypeColumnStats("c_short", wideTable.getColumn("c_short").getType()),
                new CollectionTypeColumnStats("c_arr_wide", wideTable.getColumn("c_arr_wide").getType(), false));
        SampleQueryJob job = new SampleQueryJob(connectContext, 1L, db, wideTable, stats, List.of(widePid), wideSampler);

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            Assertions.assertEquals(1, sqls.size(),
                    "ARRAY column is not isolated; both columns batch into 1 SQL");
            Assertions.assertTrue(sqls.get(0).contains("'c_short'"));
            Assertions.assertTrue(sqls.get(0).contains("'c_arr_wide'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testSampleJobsBigIntNotTreatedAsWide() {
        // BIGINT is a ScalarType but not char-family; the `!isCharFamily()` guard in
        // HyperQueryJob.isWideStringColumn must return false so BIGINT falls into the normal batched path.
        // SampleQueryJob does not normally receive non-char columns in production (ColumnClassifier
        // routes them to MetaQueryJob), but the guard is required to keep the optimization safe.
        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(1, widePid, 5000000));
            }
        };

        List<ColumnStats> stats = List.of(
                new PrimitiveTypeColumnStats("c_short", wideTable.getColumn("c_short").getType()),
                new PrimitiveTypeColumnStats("c_bigint", wideTable.getColumn("c_bigint").getType()));
        SampleQueryJob job = new SampleQueryJob(connectContext, 1L, db, wideTable, stats, List.of(widePid), wideSampler);

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            Assertions.assertEquals(1, sqls.size(),
                    "BIGINT must not trigger wide-column isolation");
            Assertions.assertTrue(sqls.get(0).contains("'c_short'"));
            Assertions.assertTrue(sqls.get(0).contains("'c_bigint'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testFullJobsWideColumnIsolation() {
        FullQueryJob job = getFullQueryJob(List.of("c_short", "c_wide"));

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 500;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            // 1 standalone SQL for the wide column + 1 SQL for the normal column.
            Assertions.assertEquals(2, sqls.size());
            // The first SQL is the standalone wide-column query (no UNION ALL).
            Assertions.assertTrue(sqls.get(0).contains("'c_wide'"));
            Assertions.assertFalse(sqls.get(0).contains("UNION ALL"),
                    "wide-column SQL must not be merged via UNION ALL");
            List<StatementBase> wideStmt = SqlParser.parse(sqls.get(0), connectContext.getSessionVariable());
            Assertions.assertTrue(wideStmt.get(0) instanceof QueryStatement);
            // The second SQL covers the narrow column.
            Assertions.assertTrue(sqls.get(1).contains("'c_short'"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testFullJobsWideColumnThresholdDisabled() {
        FullQueryJob job = getFullQueryJob(List.of("c_short", "c_wide"));

        long originalThreshold = Config.statistics_large_string_column_merge_threshold;
        int originalParallel = connectContext.getSessionVariable().getStatisticCollectParallelism();
        try {
            Config.statistics_large_string_column_merge_threshold = 0;
            connectContext.getSessionVariable().setStatisticCollectParallelism(2);
            List<String> sqls = job.buildQuerySQL();
            // Both columns share one UNION ALL'd SQL (no isolation).
            Assertions.assertEquals(1, sqls.size());
            Assertions.assertTrue(sqls.get(0).contains("'c_short'"));
            Assertions.assertTrue(sqls.get(0).contains("'c_wide'"));
            Assertions.assertTrue(sqls.get(0).contains("UNION ALL"));
        } finally {
            Config.statistics_large_string_column_merge_threshold = originalThreshold;
            connectContext.getSessionVariable().setStatisticCollectParallelism(originalParallel);
        }
    }

    @Test
    public void testIsWideStringRejectsNonBaseColumnStats() {
        // Defensive guard: a ColumnStats subtype that is NOT BaseColumnStats has no concrete
        // column-type information, so HyperQueryJob.isWideStringColumn must early-return false
        // rather than throw ClassCastException. Tested directly because this branch cannot reach
        // the query jobs through the normal ColumnClassifier path.
        ColumnStats foreign = new ColumnStats() {
            @Override
            public long getTypeSize() {
                return 0;
            }

            @Override
            public String getColumnNameStr() {
                return "c_foreign";
            }

            @Override
            public String getQuotedColumnName() {
                return "'c_foreign'";
            }

            @Override
            public String getCombinedMultiColumnKey() {
                return "";
            }

            @Override
            public String getMax() {
                return "''";
            }

            @Override
            public String getMin() {
                return "''";
            }

            @Override
            public String getCollectionSize() {
                return "0";
            }

            @Override
            public String getFullDataSize() {
                return "0";
            }

            @Override
            public String getFullNullCount() {
                return "0";
            }

            @Override
            public String getNDV() {
                return "0";
            }

            @Override
            public String getSampleDateSize(SampleInfo info) {
                return "0";
            }

            @Override
            public String getSampleNullCount(SampleInfo info) {
                return "0";
            }

            @Override
            public String getSampleNDV(SampleInfo info) {
                return "0";
            }
        };

        Assertions.assertFalse(HyperQueryJob.isWideStringColumn(foreign, 500),
                "non-BaseColumnStats input must never be classified as wide");
    }

    @Test
    public void testVirtualStatisticWithFullStatistics() throws Exception {
        // GIVEN
        final var namesAndTypes = initColumn(List.of("c7")); // c7 is array<int>
        Map<String, String> properties = Map.of(StatsConstants.UNNEST_VIRTUAL_STATISTICS, "true");

        // WHEN
        final var jobs = HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, namesAndTypes.first,
                namesAndTypes.second, List.of(pid), 1, false, properties);

        // THEN
        assertFalse(jobs.isEmpty());

        boolean containsArrayColumn = false;
        boolean containsVirtualStat = false;

        for (final var job : jobs) {
            List<String> sqls = job.buildQuerySQL();
            assertEquals(2, sqls.size());
            for (String sql : sqls) {
                Assertions.assertNotNull(getFragmentPlan(sql)); // Make sure the query can be executed.

                List<StatementBase> stmt = SqlParser.parse(sql, connectContext.getSessionVariable());

                assertFalse(stmt.isEmpty());

                // Check for regular array column
                if (sql.contains("'c7'")) {
                    containsArrayColumn = true;
                }

                // Check for virtual statistic with unnest
                if (sql.contains("unnest(`c7`)") && sql.contains("VIRTUAL_STATISTIC_c7_UNNEST")) {
                    containsVirtualStat = true;
                }
            }
        }

        assertTrue(containsArrayColumn);
        assertTrue(containsVirtualStat);
    }

    private static long getAnyTabletId(Table table) {
        final var partition = table.getPartitions().stream().findFirst().get().getDefaultPhysicalPartition();
        final var tablet = partition.getBaseIndex().getTablets().get(0);
        return tablet.getId();
    }

    @Test
    public void testVirtualStatisticWithSampleStatistics() throws Exception {
        // GIVEN
        final var namesAndTypes = initColumn(List.of("c7")); // c7 is array<int>

        new MockUp<SampleInfo>() {
            @Mock
            public List<TabletStats> getMediumHighWeightTablets() {
                return List.of(new TabletStats(getAnyTabletId(table), pid, 5000000));
            }

            @Mock
            public int getMaxSampleTabletNum() {
                return 1;
            }
        };

        Map<String, String> properties = Map.of(StatsConstants.UNNEST_VIRTUAL_STATISTICS, "true");
        List<HyperQueryJob> jobs = HyperQueryJob.createSampleQueryJobs(1L, connectContext, db, table, namesAndTypes.first,
                namesAndTypes.second, List.of(pid), 1, sampler, false, properties);

        // Should have jobs for both regular and virtual columns
        assertFalse(jobs.isEmpty());

        boolean containsArrayColumn = false;
        boolean containsVirtualStat = false;

        for (HyperQueryJob job : jobs) {
            List<String> sqls = job.buildQuerySQL();
            for (String sql : sqls) {
                Assertions.assertNotNull(getFragmentPlan(sql)); // Make sure the query can be executed.
                List<StatementBase> stmt = SqlParser.parse(sql, connectContext.getSessionVariable());
                assertFalse(stmt.isEmpty());

                // Check for regular array column (column name in the SQL as string literal or backticked)
                if (sql.contains("'c7'") || sql.contains("`c7`")) {
                    containsArrayColumn = true;
                }

                // Check for virtual statistic with unnest - in sample mode it uses subquery
                if (sql.contains("unnest(`c7`)") && sql.contains("VIRTUAL_STATISTIC_c7_UNNEST")) {
                    containsVirtualStat = true;
                }
            }
        }

        assertTrue(containsArrayColumn);
        assertTrue(containsVirtualStat);
    }

    @Test
    public void testVirtualStatisticNotAddedWhenDisabled() throws Exception {
        // GIVEN
        final var namesAndTypes = initColumn(List.of("c7")); // c7 is array<int>
        Map<String, String> properties = Map.of(StatsConstants.UNNEST_VIRTUAL_STATISTICS, "false");

        // WHEN
        List<HyperQueryJob> jobs = HyperQueryJob.createFullQueryJobs(1L, connectContext, db, table, namesAndTypes.first,
                namesAndTypes.second, List.of(pid), 1, false, properties);

        // THEN
        for (HyperQueryJob job : jobs) {
            List<String> sqls = job.buildQuerySQL();
            for (String sql : sqls) {
                Assertions.assertNotNull(getFragmentPlan(sql)); // Make sure the query can be executed.
                assertFalse(sql.contains("VIRTUAL_STATISTIC"));
                assertFalse(sql.contains("unnest"));
            }
        }
    }

    @AfterAll
    public static void afterClass() {
        FeConstants.runningUnitTest = false;
    }
}
