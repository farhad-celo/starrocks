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

package com.starrocks.statistic;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.analysis.DateLiteral;
import com.starrocks.analysis.Expr;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.Database;
import com.starrocks.catalog.MapType;
import com.starrocks.catalog.OlapTable;
import com.starrocks.catalog.Partition;
import com.starrocks.catalog.PrimitiveType;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.Type;
import com.starrocks.common.Pair;
import com.starrocks.common.jmockit.Deencapsulation;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.sql.plan.ConnectorPlanTestBase;
import com.starrocks.sql.plan.ExecPlan;
import com.starrocks.sql.plan.PlanTestBase;
import com.starrocks.statistic.sample.ColumnSampleManager;
import com.starrocks.statistic.sample.PrimitiveTypeColumnStats;
import com.starrocks.statistic.sample.SampleInfo;
import com.starrocks.statistic.sample.TabletSampleManager;
import com.starrocks.thrift.TStatisticData;
import org.apache.commons.lang3.StringUtils;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.File;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

public class StatisticsSQLTest extends PlanTestBase {
    private static long t0StatsTableId = 0;

    @TempDir
    public static File temp;

    @BeforeAll
    public static void beforeClass() throws Exception {

        PlanTestBase.beforeClass();
        GlobalStateMgr globalStateMgr = connectContext.getGlobalStateMgr();
        ConnectorPlanTestBase.mockAllCatalogs(connectContext, newFolder(temp, "junit").toURI().toString());

        StatisticsMetaManager m = new StatisticsMetaManager();
        m.createStatisticsTablesForTest();

        starRocksAssert.withTable("CREATE TABLE `stat0` (\n" +
                "  `v1` bigint NULL COMMENT \"\",\n" +
                "  `v2` bigint NULL COMMENT \"\",\n" +
                "  `v3` bigint NULL,\n" +
                "  `v4` date NULL,\n" +
                "  `v5` datetime NULL,\n" +
                "  `s1` String NULL,\n" +
                "  `j1` JSON NULL,\n" +
                "  `a1` ARRAY<int> NULL" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`, `v2`, v3)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 3\n" +
                "PROPERTIES (\n" +
                "\"replication_num\" = \"1\",\n" +
                "\"in_memory\" = \"false\"\n" +
                ");");

        starRocksAssert.withTable("CREATE TABLE `escape0['abc']` (\n" +
                "  `v1` bigint NULL COMMENT \"\",\n" +
                "  `v2['+']` bigint NULL COMMENT \"\",\n" +
                "  `v3[\" / \"]` bigint NULL,\n" +
                "  `v4[99]` String NULL,\n" +
                "  `v5('1' + '2')` String NULL,\n" +
                "  `v6['''+''']` String NULL,\n" +
                "  `v7[''''''+'''''']` JSON NULL" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 3\n" +
                "PROPERTIES (\n" +
                "\"replication_num\" = \"1\",\n" +
                "\"in_memory\" = \"false\"\n" +
                ");");

        String createStructTableSql = "CREATE TABLE struct_a(\n" +
                "a INT, \n" +
                "b STRUCT<a INT, c INT> COMMENT 'smith',\n" +
                "c STRUCT<a INT, b DOUBLE>,\n" +
                "d STRUCT<a INT, b ARRAY<STRUCT<a INT, b DOUBLE>>, c STRUCT<a INT>>,\n" +
                "struct_a STRUCT<struct_a STRUCT<struct_a INT>, other INT> COMMENT 'alias test'\n" +
                ") DISTRIBUTED BY HASH(`a`) BUCKETS 1\n" +
                "PROPERTIES (\n" +
                "    \"replication_num\" = \"1\"\n" +
                ");";
        starRocksAssert.withTable(createStructTableSql);

        starRocksAssert.withTable("CREATE TABLE `complex_table` (\n" +
                "  `v1` bigint NULL COMMENT \"\",\n" +
                "  `v2.a2.b2['+']` bigint NULL COMMENT \"\",\n" +
                "  `struct_a.c3.d3` STRUCT<struct_b int, " +
                "                          `struct_c.e3` int, " +
                "                          `struct_d.f4` struct<struct_e int, struct_f int, `struct_g.h` int>" +
                "                          > COMMENT ''\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 3\n" +
                "PROPERTIES (\n" +
                "\"replication_num\" = \"1\",\n" +
                "\"in_memory\" = \"false\"\n" +
                ");");

        OlapTable t0 = (OlapTable) globalStateMgr.getLocalMetastore().getDb("test").getTable("stat0");
        t0StatsTableId = t0.getId();
    }

    @Test
    public void testSampleStatisticsSQL() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        List<String> columnNames = Lists.newArrayList("v3", "j1", "s1");
        List<Type> columnTypes = Lists.newArrayList(Type.BIGINT, Type.JSON, Type.STRING);
        TabletSampleManager tabletSampleManager = TabletSampleManager.init(Maps.newHashMap(), t0);
        SampleInfo sampleInfo = tabletSampleManager.generateSampleInfo();

        ColumnSampleManager columnSampleManager = ColumnSampleManager.init(columnNames, columnTypes, t0,
                sampleInfo, Map.of());

        sampleInfo.generateComplexTypeColumnTask(t0.getId(), db.getId(), t0.getName(), db.getFullName(),
                columnSampleManager.getComplexTypeStats());
        String complexSql = sampleInfo.generateComplexTypeColumnTask(t0.getId(), db.getId(), t0.getName(), db.getFullName(),
                columnSampleManager.getComplexTypeStats());
        assertCContains(complexSql, "INSERT INTO _statistics_.table_statistic_v1(table_id, column_name, db_id, table_name," +
                " db_name, row_count, data_size, distinct_count, null_count, max, min, update_time) VALUES");

        String simpleSql = sampleInfo.generatePrimitiveTypeColumnTask(t0.getId(), db.getId(), t0.getName(),
                db.getFullName(), columnSampleManager.splitPrimitiveTypeStats().get(0), tabletSampleManager);
        String except = String.format("SELECT %s, '%s', %s, '%s', '%s'",
                t0.getId(), "v3", db.getId(), "test.stat0", "test");
        assertCContains(simpleSql, except);
        starRocksAssert.useDatabase("_statistics_");

        String plan = getFragmentPlan(simpleSql);

        Assertions.assertEquals(1, StringUtils.countMatches(plan, "OlapScanNode"));
        assertCContains(plan, "left(");
    }

    @Test
    public void testFullStatisticsSQL() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        List<Long> pids = t0.getPartitions().stream().map(Partition::getId).collect(Collectors.toList());

        List<String> columnNames = Lists.newArrayList("j1", "s1");
        FullStatisticsCollectJob job = new FullStatisticsCollectJob(db, t0, pids, columnNames,
                StatsConstants.AnalyzeType.FULL, StatsConstants.ScheduleType.ONCE, Maps.newHashMap());

        List<List<String>> sqls = job.buildCollectSQLList(1);
        Assertions.assertEquals(2, sqls.size());
        Assertions.assertEquals(1, sqls.get(0).size());
        Assertions.assertEquals(1, sqls.get(1).size());
        starRocksAssert.useDatabase("_statistics_");
        String plan = getFragmentPlan(sqls.get(0).get(0));
        assertCContains(plan, "count * 1024");

        plan = getFragmentPlan(sqls.get(1).get(0));
        assertCContains(plan, "left(");
        assertCContains(plan, "char_length(");
        assertCContains(plan, "hex(hll_serialize(");
    }

    @Test
    public void testFullStatisticsSQLWithStruct() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("struct_a");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        List<Long> pids = t0.getPartitions().stream().map(Partition::getId).collect(Collectors.toList());

        List<String> columnNames = Lists.newArrayList("b.a", "b.c", "d.c.a");

        FullStatisticsCollectJob job = new FullStatisticsCollectJob(db, t0, pids, columnNames, ImmutableList.of(Type.INT,
                Type.INT, Type.INT), StatsConstants.AnalyzeType.FULL, StatsConstants.ScheduleType.ONCE,
                Maps.newHashMap());

        List<List<String>> sqls = job.buildCollectSQLList(1);
        Assertions.assertEquals(3, sqls.size());
        for (int i = 0; i < sqls.size(); i++) {
            Assertions.assertEquals(1, sqls.get(i).size());
            String sql = sqls.get(i).get(0);
            starRocksAssert.useDatabase("_statistics_");
            ExecPlan plan = getExecPlan(sql);
            List<Expr> output = plan.getOutputExprs();
            Assertions.assertEquals(output.get(2).getType().getPrimitiveType(), Type.STRING.getPrimitiveType());
            assertCContains(plan.getColNames().get(2).replace("\\", ""), columnNames.get(i));
        }
    }

    @Test
    public void testHistogramStatisticsSQLWithStruct() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("struct_a");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        List<String> columnNames = Lists.newArrayList("b.a", "b.c", "d.c.a");
        HistogramStatisticsCollectJob histogramStatisticsCollectJob = new HistogramStatisticsCollectJob(
                db, t0, Lists.newArrayList("b.a", "b.c", "d.c.a"),
                Lists.newArrayList(Type.INT, Type.INT, Type.INT), StatsConstants.ScheduleType.ONCE,
                Maps.newHashMap());
        for (String col : columnNames) {
            String sql = Deencapsulation.invoke(histogramStatisticsCollectJob, "buildCollectMCV",
                    db, t0, 3L, col, 0.1);
            starRocksAssert.useDatabase("_statistics_");
            String plan = getFragmentPlan(sql);
            assertCContains(plan, "0:OlapScanNode\n" +
                    "     TABLE: struct_a");
        }

        for (String col : columnNames) {
            String sql = Deencapsulation.invoke(histogramStatisticsCollectJob, "buildCollectHistogram",
                    db, t0, 0.1, 10L, ImmutableMap.of("d.c.a", "100"), col, Type.INT);
            sql = sql.substring(sql.indexOf("SELECT"));
            starRocksAssert.useDatabase("_statistics_");
            String plan = getFragmentPlan(sql);
            assertCContains(plan, "AGGREGATE (update finalize)\n" +
                    "  |  output: histogram");

            String querySql = Deencapsulation.invoke(histogramStatisticsCollectJob, "buildQueryHistogram",
                    db, t0, 0.1, 10L, ImmutableMap.of("d.c.a", "100"), col, Type.INT);
            plan = getFragmentPlan(querySql);
            assertCContains(plan, "AGGREGATE (update finalize)\n" +
                    "  |  output: histogram");
        }
    }

    @Test
    public void testHistogramMcvScopeUsesSingleSampledBucket() throws Exception {
        // Given a histogram job whose scope excludes buckets
        // WHEN sampled bounds are available THEN the single bucket spans them and carries the non-MCV
        // row count END

        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        HistogramStatisticsCollectJob job = mcvOnlyHistogramJob(db, t0, "v1", Type.BIGINT);

        String sql = Deencapsulation.invoke(job, "buildCollectSingleBucket",
                db, t0, 0.1, ImmutableMap.of("1", "10", "2", "20"), "v1", Type.BIGINT,
                Optional.of(Pair.create("1", "100")));

        // 10 + 20 MCV rows are excluded from the bucket, and count() is divided by the sample ratio.
        Assertions.assertTrue(sql.contains("concat('[[\"1\",\"100\",', " +
                "cast(cast(greatest(0, count(`v1`) / cast(0.1 as double) - 30) as bigint) as varchar), ',0]]')"), sql);
        Assertions.assertTrue(sql.contains("'[[\"1\",\"10\"],[\"2\",\"20\"]]'"), sql);
        Assertions.assertFalse(sql.contains("histogram("), sql);
    }

    @Test
    public void testHistogramSingleBucketUsesPlaceholderBoundsForStringColumn() throws Exception {
        // Given a histogram job on a char-family column THEN no MIN/MAX query runs and the single bucket
        //      falls back to the infinite placeholder bounds END

        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        HistogramStatisticsCollectJob job = mcvOnlyHistogramJob(db, t0, "s1", Type.STRING);

        Optional<Pair<String, String>> bounds = Deencapsulation.invoke(job, "sampleColumnMinMax",
                connectContext, new StatisticExecutor(), "s1", Type.STRING, 0.1, ImmutableMap.of("a", "10"));
        Assertions.assertTrue(bounds.isEmpty());

        String sql = Deencapsulation.invoke(job, "buildCollectSingleBucket",
                db, t0, 0.1, ImmutableMap.of("a", "10"), "s1", Type.STRING, bounds);

        Assertions.assertTrue(sql.contains("concat('[[\"Infinity\",\"Infinity\",', " +
                "cast(cast(greatest(0, count(`s1`) / cast(0.1 as double) - 10) as bigint) as varchar), ',0]]')"), sql);
        Assertions.assertFalse(sql.contains("histogram("), sql);
    }

    @Test
    public void testHistogramSingleBucketUsesWidestDateBoundsWhenSampleIsEmpty() throws Exception {
        // Given a histogram job whose scope excludes buckets on a date column
        // WHEN the sampled MIN/MAX comes back empty
        // THEN the single bucket spans the type's whole domain, not the unparseable placeholder END

        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        Table t0 = db.getTable("stat0");

        assertSingleBucketDateBounds(db, t0, "v4", Type.DATE);
        assertSingleBucketDateBounds(db, t0, "v5", Type.DATETIME);
    }

    private static void assertSingleBucketDateBounds(Database db, Table t0, String columnName, Type dateType) {
        // The fallback bounds come from DateLiteral's own MIN/MAX for the type, so derive them the same way here.
        String expectedMin = DateLiteral.createMinValue(dateType).getStringValue();
        String expectedMax = DateLiteral.createMaxValue(dateType).getStringValue();

        HistogramStatisticsCollectJob job = mcvOnlyHistogramJob(db, t0, columnName, dateType);

        Optional<Pair<String, String>> bounds = Deencapsulation.invoke(job, "parseSampledBounds",
                Lists.<TStatisticData>newArrayList(), columnName, dateType);

        Assertions.assertTrue(bounds.isPresent(), columnName);
        Assertions.assertEquals(expectedMin, bounds.get().first, columnName);
        Assertions.assertEquals(expectedMax, bounds.get().second, columnName);

        String sql = Deencapsulation.invoke(job, "buildCollectSingleBucket",
                db, t0, 1.0, ImmutableMap.<String, String>of(), columnName, dateType, bounds);

        Assertions.assertTrue(sql.contains("concat('[[\"" + expectedMin + "\",\"" + expectedMax + "\",'"), sql);
    }

    @Test
    public void testHistogramSampleMinMaxSQL() throws Exception {
        // Given a histogram job whose scope excludes buckets
        // THEN the bucket bounds come from a sampled MIN/MAX over the column END

        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        HistogramStatisticsCollectJob job = mcvOnlyHistogramJob(db, t0, "v1", Type.BIGINT);

        String sql = Deencapsulation.invoke(job, "buildSampleMinMax",
                db, t0, "v1", Type.BIGINT, 0.1, ImmutableMap.of("1", "10", "2", "20"));

        Assertions.assertTrue(sql.contains("cast(" + StatsConstants.STATISTIC_HISTOGRAM_VERSION + " as INT)"), sql);
        // Date and datetime columns take this same shape - getMinMaxFunction only wraps char-family columns
        // differently, and those never reach a bounds query at all.
        Assertions.assertTrue(sql.contains("cast(IFNULL(MIN(`column_key`), '') as varchar)"), sql);
        Assertions.assertTrue(sql.contains("cast(IFNULL(MAX(`column_key`), '') as varchar)"), sql);
        Assertions.assertTrue(sql.contains("SAMPLE('percent'='10')"), sql);
        // The bucket's count subtracts the MCV rows, so its bounds are sampled over the same population.
        Assertions.assertTrue(sql.contains("and `v1` not in (1,2)"), sql);
        // No LIMIT: without an ORDER BY it would bias min/max towards an arbitrary scan-order prefix.
        Assertions.assertFalse(sql.toLowerCase().contains("limit"), sql);
    }

    private static HistogramStatisticsCollectJob mcvOnlyHistogramJob(Database db, Table table, String columnName,
                                                                     Type columnType) {
        return new HistogramStatisticsCollectJob(db, table, Lists.newArrayList(columnName),
                Lists.newArrayList(columnType), StatsConstants.ScheduleType.ONCE,
                ImmutableMap.of(StatsConstants.HISTOGRAM_STATS_SCOPE, StatsConstants.HISTOGRAM_STATS_SCOPE_MCV));
    }

    @Test
    public void testHiveHistogramStatisticsSQLWithStruct() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getMetadataMgr().getTable(connectContext, "hive0", "subfield_db",
                "subfield");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "subfield_db");

        List<String> columnNames = Lists.newArrayList("col_struct.c0", "col_struct.c1.c11");
        ExternalHistogramStatisticsCollectJob hiveHistogramStatisticsCollectJob = new ExternalHistogramStatisticsCollectJob(
                "hive0", db, t0, columnNames, Lists.newArrayList(Type.INT, Type.INT),
                StatsConstants.AnalyzeType.HISTOGRAM, StatsConstants.ScheduleType.ONCE,
                Maps.newHashMap());
        for (String col : columnNames) {
            String sql = Deencapsulation.invoke(hiveHistogramStatisticsCollectJob, "buildCollectMCV",
                    db, t0, 3L, col);
            starRocksAssert.useDatabase("_statistics_");
            String plan = getFragmentPlan(sql);
            assertCContains(plan, " 0:HdfsScanNode\n" +
                    "     TABLE: subfield");
        }

        for (String col : columnNames) {
            String sql = Deencapsulation.invoke(hiveHistogramStatisticsCollectJob, "buildCollectHistogram",
                    db, t0, 0.1, 10L, ImmutableMap.of("col_struct.c1.c11", "100"), col, Type.INT);
            sql = sql.substring(sql.indexOf("SELECT"));
            starRocksAssert.useDatabase("_statistics_");
            String plan = getFragmentPlan(sql);
            assertCContains(plan, "4:AGGREGATE (update finalize)\n" +
                    "  |  output: histogram");

            String querySql = Deencapsulation.invoke(hiveHistogramStatisticsCollectJob, "buildQueryHistogram",
                    db, t0, 0.1, 10L, ImmutableMap.of("col_struct.c1.c11", "100"), col, Type.INT);
            plan = getFragmentPlan(querySql);
            assertCContains(plan, "4:AGGREGATE (update finalize)\n" +
                    "  |  output: histogram");
        }
    }

    @Test
    public void testExternalHistogramMcvScopeUsesSingleSampledBucket() throws Exception {
        Table region = GlobalStateMgr.getCurrentState().getMetadataMgr()
                .getTable(connectContext, "hive0", "tpch", "region");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "tpch");

        ExternalHistogramStatisticsCollectJob job = mcvOnlyExtHistogramJob(db, region);

        String sql = Deencapsulation.invoke(job, "buildCollectSingleBucket",
                db, region, ImmutableMap.of("1", "10"), "r_regionkey", Type.INT,
                Optional.of(Pair.create("0", "4")));

        Assertions.assertTrue(sql.contains("concat('[[\"0\",\"4\",', " +
                "cast(greatest(0, count(`r_regionkey`) - 10) as varchar), ',0]]')"), sql);
        Assertions.assertTrue(sql.contains("'[[\"1\",\"10\"]]'"), sql);
        Assertions.assertTrue(sql.contains("FROM `hive0`.`tpch`.`region`"), sql);
        Assertions.assertFalse(sql.contains("histogram("), sql);

        starRocksAssert.useDatabase("_statistics_");
        String plan = getFragmentPlan(sql.substring(sql.indexOf("SELECT")));
        assertCContains(plan, "TABLE: region");
    }

    @Test
    public void testExternalHistogramSampleMinMaxSQL() throws Exception {
        Table region = GlobalStateMgr.getCurrentState().getMetadataMgr()
                .getTable(connectContext, "hive0", "tpch", "region");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "tpch");

        ExternalHistogramStatisticsCollectJob job = mcvOnlyExtHistogramJob(db, region);

        String sql = Deencapsulation.invoke(job, "buildSampleMinMax",
                db, region, "r_regionkey", Type.INT, 0.1, ImmutableMap.of("1", "10"));

        Assertions.assertTrue(sql.contains("cast(IFNULL(MIN(`column_key`), '') as varchar)"), sql);
        Assertions.assertTrue(sql.contains("cast(IFNULL(MAX(`column_key`), '') as varchar)"), sql);
        Assertions.assertTrue(sql.contains(
                "where rand() <= 0.1 and `r_regionkey` is not null  and `r_regionkey` not in (1)"), sql);
        // No LIMIT: without an ORDER BY it would bias min/max towards an arbitrary scan-order prefix.
        Assertions.assertFalse(sql.toLowerCase().contains("limit"), sql);
        // ORDER BY would bias MAX towards the smallest sampled rows, and the bucket aggregate is what we are avoiding.
        Assertions.assertFalse(sql.toLowerCase().contains("order by"), sql);
        Assertions.assertFalse(sql.contains("histogram("), sql);

        starRocksAssert.useDatabase("_statistics_");
        String plan = getFragmentPlan(sql);
        assertCContains(plan, "TABLE: region");
        Assertions.assertTrue(plan.contains("min(") && plan.contains("max("), plan);
    }

    @Test
    public void testExternalHistogramSampleMinMaxSQLForDateColumn() throws Exception {
        Table orders = GlobalStateMgr.getCurrentState().getMetadataMgr()
                .getTable(connectContext, "hive0", "tpch", "orders");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "tpch");

        ExternalHistogramStatisticsCollectJob job = new ExternalHistogramStatisticsCollectJob(
                "hive0", db, orders, Lists.newArrayList("o_orderdate"), Lists.<Type>newArrayList(Type.DATE),
                StatsConstants.AnalyzeType.HISTOGRAM, StatsConstants.ScheduleType.ONCE,
                ImmutableMap.of(StatsConstants.HISTOGRAM_STATS_SCOPE, StatsConstants.HISTOGRAM_STATS_SCOPE_MCV));

        String sql = Deencapsulation.invoke(job, "buildSampleMinMax",
                db, orders, "o_orderdate", Type.DATE, 0.1, ImmutableMap.of("1993-01-01", "10"));

        Assertions.assertTrue(sql.contains("cast(IFNULL(MIN(`column_key`), '') as varchar)"), sql);

        // IFNULL mixes DATE with a VARCHAR literal, so the real check is that this still analyzes and plans.
        starRocksAssert.useDatabase("_statistics_");
        String plan = getFragmentPlan(sql);
        assertCContains(plan, "TABLE: orders");
        Assertions.assertTrue(plan.contains("min(") && plan.contains("max("), plan);
    }

    @Test
    public void testExternalHistogramSampleMinMaxSkipsUnparseableTypes() throws Exception {
        Table region = GlobalStateMgr.getCurrentState().getMetadataMgr()
                .getTable(connectContext, "hive0", "tpch", "region");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "tpch");

        ExternalHistogramStatisticsCollectJob job = mcvOnlyExtHistogramJob(db, region);

        // Char-family and boolean bounds cannot be read back (Double.parseDouble on "AFRICA" / "TRUE" throws), so
        // sampleColumnMinMax refuses them before it even runs the MIN/MAX query - which is why the bucket builder
        // no longer re-checks.
        for (Type unparseable : Lists.<Type>newArrayList(Type.VARCHAR, Type.BOOLEAN)) {
            Optional<Pair<String, String>> bounds = Deencapsulation.invoke(job, "sampleColumnMinMax",
                    connectContext, new StatisticExecutor(), "r_name", unparseable, 0.1,
                    ImmutableMap.of("AFRICA", "10"));

            Assertions.assertTrue(bounds.isEmpty());
        }
    }

    @Test
    public void testExternalHistogramSingleBucketUsesWidestDateBoundsWhenSampleIsEmpty() throws Exception {
        // Given an external histogram job whose scope excludes buckets on a date column
        // WHEN the sampled MIN/MAX comes back empty
        // THEN the single bucket spans the type's whole domain, not the unparseable placeholder END

        Table lineitem = GlobalStateMgr.getCurrentState().getMetadataMgr()
                .getTable(connectContext, "hive0", "tpch", "lineitem");
        Database db = GlobalStateMgr.getCurrentState().getMetadataMgr().getDb(connectContext, "hive0", "tpch");

        ExternalHistogramStatisticsCollectJob job = mcvOnlyExtHistogramJob(db, lineitem);

        Optional<Pair<String, String>> bounds = Deencapsulation.invoke(job, "parseSampledBounds",
                Lists.<TStatisticData>newArrayList(), "l_shipdate", Type.DATE);

        String expectedMin = DateLiteral.createMinValue(Type.DATE).getStringValue();
        String expectedMax = DateLiteral.createMaxValue(Type.DATE).getStringValue();

        Assertions.assertTrue(bounds.isPresent());
        Assertions.assertEquals(expectedMin, bounds.get().first);
        Assertions.assertEquals(expectedMax, bounds.get().second);

        String sql = Deencapsulation.invoke(job, "buildCollectSingleBucket",
                db, lineitem, ImmutableMap.<String, String>of(), "l_shipdate", Type.DATE, bounds);

        Assertions.assertTrue(sql.contains("concat('[[\"" + expectedMin + "\",\"" + expectedMax + "\",'"), sql);
        Assertions.assertFalse(sql.contains("Infinity"), sql);
    }

    private static ExternalHistogramStatisticsCollectJob mcvOnlyExtHistogramJob(Database db, Table region) {
        return new ExternalHistogramStatisticsCollectJob(
                "hive0", db, region, Lists.newArrayList("r_regionkey"), Lists.<Type>newArrayList(Type.INT),
                StatsConstants.AnalyzeType.HISTOGRAM, StatsConstants.ScheduleType.ONCE,
                ImmutableMap.of(StatsConstants.HISTOGRAM_STATS_SCOPE, StatsConstants.HISTOGRAM_STATS_SCOPE_MCV));
    }

    @Test
    public void testEscapeFullSQL() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("escape0['abc']");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        List<Long> pids = t0.getPartitions().stream().map(Partition::getId).collect(Collectors.toList());

        List<String> columnNames = t0.getColumns().stream().map(Column::getName).collect(Collectors.toList());
        FullStatisticsCollectJob job = new FullStatisticsCollectJob(db, t0, pids, columnNames,
                StatsConstants.AnalyzeType.FULL, StatsConstants.ScheduleType.ONCE, Maps.newHashMap());

        List<List<String>> sqls = job.buildCollectSQLList(1);
        Assertions.assertEquals(7, sqls.size());

        for (int i = 0; i < sqls.size(); i++) {
            Assertions.assertEquals(1, sqls.get(i).size());
            String sql = sqls.get(i).get(0);
            starRocksAssert.useDatabase("_statistics_");
            ExecPlan plan = getExecPlan(sql);
            List<Expr> output = plan.getOutputExprs();
            Assertions.assertEquals(output.get(2).getType().getPrimitiveType(), Type.STRING.getPrimitiveType());
            assertCContains(plan.getColNames().get(2).replace("\\", ""), columnNames.get(i));
        }
    }

    @Test
    public void testEscapeSampleSQL() throws Exception {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("escape0['abc']");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");

        for (Column column : t0.getColumns()) {
            if (!column.getType().canStatistic()) {
                continue;
            }
            TabletSampleManager tabletSampleManager = TabletSampleManager.init(Maps.newHashMap(), t0);
            SampleInfo sampleInfo = tabletSampleManager.generateSampleInfo();
            String sql = sampleInfo.generatePrimitiveTypeColumnTask(t0.getId(), db.getId(), t0.getName(), db.getFullName(),
                    Lists.newArrayList(new PrimitiveTypeColumnStats(column.getName(), column.getType())),
                    tabletSampleManager);
            starRocksAssert.useDatabase("_statistics_");
            ExecPlan plan = getExecPlan(sql);
            List<Expr> output = plan.getOutputExprs();
            Assertions.assertEquals(output.get(1).getType().getPrimitiveType(), Type.STRING.getPrimitiveType());
            Assertions.assertEquals(output.get(3).getType().getPrimitiveType(), Type.STRING.getPrimitiveType());
            Assertions.assertEquals(output.get(4).getType().getPrimitiveType(), Type.STRING.getPrimitiveType());

            assertCContains(plan.getColNames().get(1).replace("\\", ""), column.getName());
            assertCContains(plan.getColNames().get(3).replace("\\", ""), "escape0['abc']");
        }
    }

    @Test
    public void testDropPartitionSQL() throws Exception {
        starRocksAssert.useDatabase("_statistics_");
        String sql = StatisticSQLBuilder.buildDropPartitionSQL(Lists.newArrayList(1L, 2L, 3L));
        String plan = getFragmentPlan(sql);
        assertCContains(plan, "partition_id IN (1, 2, 3)");

    }

    @Test
    public void testDropInvalidPartitionSQL() throws Exception {
        starRocksAssert.useDatabase("_statistics_");
        String sql = StatisticSQLBuilder.buildDropTableInvalidPartitionSQL(Lists.newArrayList(4L, 5L, 6L),
                Lists.newArrayList(1L, 2L, 3L));
        String plan = getFragmentPlan(sql);
        assertCContains(plan, "table_id IN (4, 5, 6)");
        assertCContains(plan, "partition_id NOT IN (1, 2, 3)");
    }

    @Test
    public void testCacheQueryColumnStatics() {
        String sql = StatisticSQLBuilder.buildQueryFullStatisticsSQL(2L, Lists.newArrayList("col1", "col2"),
                Lists.newArrayList(Type.INT, Type.INT));
        assertContains(sql, "table_id = 2 and column_name in (\"col1\", \"col2\")");
        Assertions.assertEquals(0, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryFullStatisticsSQL(2L,
                Lists.newArrayList("col1", "col2", "col3"),
                Lists.newArrayList(Type.INT, Type.BIGINT, Type.LARGEINT));
        assertContains(sql, "table_id = 2 and column_name in (\"col1\", \"col2\")");
        assertContains(sql, "table_id = 2 and column_name in (\"col3\")");
        Assertions.assertEquals(1, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryFullStatisticsSQL(2L,
                Lists.newArrayList("col1", "col2", "col3", "col4", "col5", "col6", "col7"),
                Lists.newArrayList(Type.INT, Type.BIGINT, Type.LARGEINT, Type.STRING, Type.VARCHAR, Type.ARRAY_DATE,
                        Type.DATE));
        assertContains(sql, "table_id = 2 and column_name in (\"col1\", \"col2\")");
        assertContains(sql, "table_id = 2 and column_name in (\"col3\")");
        assertContains(sql, "table_id = 2 and column_name in (\"col4\", \"col5\")");
        assertContains(sql, "table_id = 2 and column_name in (\"col7\")");
        assertContains(sql, "table_id = 2 and column_name in (\"col6\")");
        Assertions.assertEquals(4, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryFullStatisticsSQL(2L,
                Lists.newArrayList("col1", "col2", "col3", "col4", "col5", "col6", "col7"),
                Lists.newArrayList(ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 4, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 4, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 5, 2),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL64, 14, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL64, 8, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 21, 6),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 22, 7),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 23, 8)));
        assertContains(sql, "table_id = 2 and column_name in (\"col1\", \"col2\")");
        Assertions.assertEquals(5, StringUtils.countMatches(sql, "UNION ALL"));
    }

    @Test
    public void testQueryTableStatisticsFiltersZeroRowCount() {
        String sql = StatisticSQLBuilder.buildQueryTableStatisticsSQL(2L, Lists.newArrayList());
        assertContains(sql, "WHERE table_id = 2 AND row_count > 0");

        sql = StatisticSQLBuilder.buildQueryTableStatisticsSQL(2L, Lists.newArrayList(10L, 20L));
        assertContains(sql, "WHERE table_id = 2 and partition_id in (10, 20) AND row_count > 0");

        sql = StatisticSQLBuilder.buildQueryTableStatisticsSQL(2L, 10L);
        assertContains(sql, "WHERE table_id = 2 and partition_id = 10 AND row_count > 0");
    }

    @Test
    public void testCacheExternalQueryColumnStatics() {
        String sql = StatisticSQLBuilder.buildQueryExternalFullStatisticsSQL("a", Lists.newArrayList("col1", "col2"),
                Lists.newArrayList(Type.INT, Type.INT));
        assertContains(sql, "table_uuid = \"a\" and column_name in (\"col1\", \"col2\")");
        Assertions.assertEquals(0, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryExternalFullStatisticsSQL("a",
                Lists.newArrayList("col1", "col2", "col3"),
                Lists.newArrayList(Type.INT, Type.BIGINT, Type.LARGEINT));
        assertContains(sql, "table_uuid = \"a\" and column_name in (\"col1\", \"col2\")");
        assertContains(sql, "table_uuid = \"a\" and column_name in (\"col3\")");
        Assertions.assertEquals(1, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryExternalFullStatisticsSQL("a",
                Lists.newArrayList("col1", "col2", "col3", "col4", "col5", "col6", "col7"),
                Lists.newArrayList(Type.INT, Type.BIGINT, Type.LARGEINT, Type.STRING, Type.VARCHAR, Type.ARRAY_DATE,
                        Type.DATE));
        assertContains(sql, "column_name in (\"col1\", \"col2\")");
        assertContains(sql, "column_name in (\"col3\")");
        assertContains(sql, "column_name in (\"col4\", \"col5\", \"col6\")");
        assertContains(sql, "column_name in (\"col7\")");
        Assertions.assertEquals(3, StringUtils.countMatches(sql, "UNION ALL"));

        sql = StatisticSQLBuilder.buildQueryExternalFullStatisticsSQL("a",
                Lists.newArrayList("col1", "col2", "col3", "col4", "col5", "col6", "col7"),
                Lists.newArrayList(ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 4, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 4, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL32, 5, 2),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL64, 14, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL64, 8, 3),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 21, 6),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 22, 7),
                        ScalarType.createDecimalV3Type(PrimitiveType.DECIMAL128, 23, 8)));
        assertContains(sql, "column_name in (\"col1\", \"col2\")");
        Assertions.assertEquals(5, StringUtils.countMatches(sql, "UNION ALL"));
    }

    @Test
    public void testExternalTableCollectionStatsType() {
        String sql = StatisticSQLBuilder.buildQueryExternalFullStatisticsSQL("a", Lists.newArrayList("col1", "col2"),
                Lists.newArrayList(Type.ARRAY_INT, new MapType(Type.INT, Type.STRING)));
        assertContains(sql, "cast(max(cast(nullif(max, '') as string)) as string)," +
                " cast(min(cast(nullif(min, '') as string)) as string)");
    }

    @Test
    public void testQuota() {
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("complex_table");
        assertContains(StatisticUtils.quoting(t0, "v2.a2.b2['+']"), "`v2.a2.b2['+']`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3"), "`struct_a.c3.d3`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3.struct_b"), "`struct_a.c3.d3`.`struct_b`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3.struct_c.e3"), "`struct_a.c3.d3`.`struct_c.e3`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3.struct_d.f4"), "`struct_a.c3.d3`.`struct_d.f4`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3.struct_d.f4.struct_e"),
                "`struct_a.c3.d3`.`struct_d.f4`.`struct_e`");
        assertContains(StatisticUtils.quoting(t0, "struct_a.c3.d3.struct_d.f4.struct_g.h"),
                "`struct_a.c3.d3`.`struct_d.f4`.`struct_g.h`");
    }

    @Test
    public void testVirtualStatisticWithFullStatistics() throws Exception {
        // GIVEN
        Table t0 = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test").getTable("stat0");
        Database db = GlobalStateMgr.getCurrentState().getLocalMetastore().getDb("test");
        List<Long> pids = t0.getPartitions().stream().map(Partition::getId).collect(Collectors.toList());

        List<String> columnNames = Lists.newArrayList("a1");
        FullStatisticsCollectJob job = new FullStatisticsCollectJob(db, t0, pids, columnNames,
                StatsConstants.AnalyzeType.FULL, StatsConstants.ScheduleType.ONCE,
                Maps.newHashMap(Map.of(StatsConstants.UNNEST_VIRTUAL_STATISTICS, "true")));

        // WHEN
        final var batches = job.buildCollectSQLList(1);

        // THEN
        boolean containsArrayColumn = false;
        boolean containsVirtualStat = false;
        for (final var batch : batches) {
            for (final var sql : batch) {
                Assertions.assertNotNull(getFragmentPlan(sql));

                if (sql.contains("`a1`")) {
                    containsArrayColumn = true;
                }

                if (sql.contains("unnest(`a1`)") && sql.contains("VIRTUAL_STATISTIC_a1_UNNEST")) {
                    containsVirtualStat = true;
                }
            }
        }

        Assertions.assertTrue(containsVirtualStat);
        Assertions.assertTrue(containsArrayColumn);
    }

    private static File newFolder(File root, String... subDirs) throws IOException {
        String subFolder = String.join("/", subDirs);
        File result = new File(root, subFolder);
        if (!result.mkdirs()) {
            throw new IOException("Couldn't create folders " + root);
        }
        return result;
    }
}
