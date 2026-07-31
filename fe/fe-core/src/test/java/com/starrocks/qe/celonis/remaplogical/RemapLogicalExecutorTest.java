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

package com.starrocks.qe.celonis.remaplogical;

import com.starrocks.common.Config;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.remaplogical.RemapLogicalStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Integration tests for REMAP LOGICAL parsing, analysis, logical-schema extensions, and SQL formatting.
 *
 * Database is "test"; available tables: t0(v1,v2,v3 bigint), t1(v4,v5,v6 bigint).
 */
class RemapLogicalExecutorTest extends PlanTestBase {

    @BeforeEach
    void setUpLimitConfig() {
        Config.validate_max_limit = 500;
    }

    @AfterEach
    void resetLimitConfig() {
        Config.validate_max_limit = 0;
    }

    private String run(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);
        return RemapLogicalExecutor.execute((RemapLogicalStmt) parsedStatement, connectContext);
    }

    @Test
    void remapsExistingTableOnly() {
        String sql = run("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0)");
        assertTrue(sql.contains("FROM `physical_t0`"), sql);
        assertTrue(sql.contains("`physical_t0`.`v1`"), sql);
    }

    @Test
    void requiresExplicitTopLevelLimit() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT v1 FROM t0 MAPPINGS (TABLE test.t0 TO physical_t0)"));
        assertTrue(exception.getMessage().contains("requires an explicit top-level LIMIT"),
                exception.getMessage());
    }

    @Test
    void rejectsOversizedTopLevelLimit() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 10, 1000 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0)"));
        assertTrue(exception.getMessage().contains("top-level LIMIT must be no greater than 500"),
                exception.getMessage());
    }

    @Test
    void preservesSmallerTopLevelLimit() {
        String sql = run("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 25 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0)");
        assertTrue(sql.endsWith(" LIMIT 25"), sql);
    }

    @Test
    void requiresTopLevelLimitEvenWhenNestedSelectHasLimit() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT q.v1 FROM (SELECT v1 FROM t0 LIMIT 1000) q " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0)"));
        assertTrue(exception.getMessage().contains("requires an explicit top-level LIMIT"),
                exception.getMessage());
    }

    @Test
    void allowsNestedSelectLimitAboveCapWhenTopLevelLimitIsValid() {
        String sql = run("REMAP LOGICAL SELECT q.v1 FROM (SELECT v1 FROM t0 LIMIT 1000) q LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0)");
        assertTrue(sql.contains("LIMIT 1000"), sql);
        assertTrue(sql.endsWith(" LIMIT 500"), sql);
    }

    @Test
    void remapsRealAndLogicalColumns() {
        String sql = run("REMAP LOGICAL SELECT logical_v1, v2 FROM t0 " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                "COLUMN test.t0.logical_v1 TO physical_v1, COLUMN test.t0.v2 TO physical_v2) " +
                "EXTENSIONS (test.t0.logical_v1 : BIGINT)");
        assertTrue(sql.contains("`physical_t0`.`physical_v1` AS `logical_v1`"), sql);
        assertTrue(sql.contains("`physical_t0`.`physical_v2` AS `v2`"), sql);
        assertTrue(sql.contains("FROM `physical_t0`"), sql);
    }

    @Test
    void remapsSelectStarToExplicitColumns() {
        String sql = run("REMAP LOGICAL SELECT * FROM t0 " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                "COLUMN test.t0.v1 TO physical_v1, COLUMN test.t0.v2 TO physical_v2)");
        assertFalse(sql.contains("*"), sql);
        assertTrue(sql.contains("`physical_t0`.`physical_v1` AS `v1`"), sql);
        assertTrue(sql.contains("`physical_t0`.`physical_v2` AS `v2`"), sql);
        assertTrue(sql.contains("`physical_t0`.`v3`"), sql);
        assertTrue(sql.contains("FROM `physical_t0`"), sql);
    }

    @Test
    void remapsAliasedLogicalReferencesThroughSourceTable() {
        String sql = run("REMAP LOGICAL SELECT l.logical_v1 FROM t0 l " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, COLUMN test.t0.logical_v1 TO physical_v1) " +
                "EXTENSIONS (test.t0.logical_v1 : BIGINT)");
        assertTrue(sql.contains("`l`.`physical_v1` AS `logical_v1`"), sql);
        assertTrue(sql.contains("FROM `physical_t0` AS `l`"), sql);
    }

    @Test
    void remapsQualifiedAliasedLogicalReferencesWithAliasOnlyQualifier() {
        String sql = run("REMAP LOGICAL SELECT l.logical_v1 FROM test.t0 AS l " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, COLUMN test.t0.logical_v1 TO physical_v1) " +
                "EXTENSIONS (test.t0.logical_v1 : BIGINT)");
        assertTrue(sql.contains("`l`.`physical_v1` AS `logical_v1`"), sql);
        assertFalse(sql.contains("`test`.`l`.`physical_v1`"), sql);
        assertTrue(sql.contains("FROM `physical_t0` AS `l`"), sql);
    }

    @Test
    void remapsExpressionSlotsWithoutInlineAliases() {
        String sql = run("REMAP LOGICAL SELECT MAX(l.logical_v1), l.logical_v1 " +
                "FROM test.t0 AS l JOIN test.t1 AS r ON l.logical_v1 = r.v4 " +
                "WHERE l.logical_v1 > 0 GROUP BY l.logical_v1 LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, TABLE test.t1 TO physical_t1, " +
                "COLUMN test.t0.logical_v1 TO physical_v1, COLUMN test.t1.v4 TO physical_v4) " +
                "EXTENSIONS (test.t0.logical_v1 : BIGINT)");
        assertTrue(sql.contains("max(`l`.`physical_v1`)"), sql);
        assertTrue(sql.contains("`l`.`physical_v1` AS `logical_v1`"), sql);
        assertFalse(sql.contains("MAX(`l`.`physical_v1` AS `logical_v1`)"), sql);
        assertFalse(sql.contains("ON `l`.`physical_v1` AS"), sql);
        assertFalse(sql.contains("WHERE `l`.`physical_v1` AS"), sql);
        assertFalse(sql.contains("GROUP BY `l`.`physical_v1` AS"), sql);
    }

    @Test
    void remapsOrderBySlotWithAliasOnlyQualifier() {
        String sql = run("REMAP LOGICAL SELECT `a`.`c` FROM `T` AS `a` ORDER BY `a`.`c` LIMIT 500 " +
                "MAPPINGS (TABLE test.T TO T_WRAPPED, COLUMN test.T.c TO C_WRAPPED) " +
                "EXTENSIONS (test.T.c : VARCHAR(65533))");
        assertTrue(sql.contains("`a`.`C_WRAPPED` AS `c`"), sql);
        assertTrue(sql.contains("FROM `T_WRAPPED` AS `a`"), sql);
        assertTrue(sql.contains("ORDER BY `a`.`C_WRAPPED` ASC"), sql);
        assertFalse(sql.contains("`test`.`a`.`C_WRAPPED`"), sql);
    }

    @Test
    void remapsNestedOrderByAgainstVisibleSubqueryAlias() {
        String sql = run("REMAP LOGICAL SELECT `outer_q`.`salesOrder` " +
                "FROM (" +
                "SELECT `inner_q`.`sales_order` AS `salesOrder` " +
                "FROM `SalesOrderAlias` AS `inner_q`" +
                ") AS `outer_q` " +
                "ORDER BY `outer_q`.`salesOrder` LIMIT 500 " +
                "MAPPINGS (TABLE test.SalesOrderAlias TO sales_order_wrapper, " +
                "COLUMN test.SalesOrderAlias.sales_order TO salesOrder) " +
                "EXTENSIONS (test.SalesOrderAlias.sales_order : VARCHAR(65533))");
        assertTrue(sql.contains("FROM `sales_order_wrapper` AS `inner_q`"), sql);
        assertTrue(sql.contains("ORDER BY `outer_q`.`salesOrder` ASC"), sql);
        assertFalse(sql.contains("`test`.`outer_q`.`salesOrder`"), sql);
    }

    @Test
    void remapsCaseOnlyLogicalTableSpellingThroughMapping() {
        String sql = run("REMAP LOGICAL SELECT v1 FROM T0 LIMIT 500 " +
                "MAPPINGS (TABLE test.T0 TO wrapped_t0)");
        assertTrue(sql.contains("`wrapped_t0`.`v1`"), sql);
        assertTrue(sql.contains("FROM `wrapped_t0`"), sql);
    }

    @Test
    void remapsCaseDifferentAliasedQualifierAndTableSpelling() {
        String sql = run("REMAP LOGICAL SELECT " +
                "`caseextrainfo`.`caseextrainfo` AS `PK`, " +
                "`caseextrainfo`.`extra` AS `col` " +
                "FROM `CASEEXTRAINFO` AS `caseextrainfo` " +
                "LIMIT 500 " +
                "MAPPINGS (" +
                "TABLE test.CASEEXTRAINFO TO wrapped_caseextrainfo, " +
                "COLUMN test.CASEEXTRAINFO.caseextrainfo TO caseextrainfo, " +
                "COLUMN test.CASEEXTRAINFO.extra TO extra) " +
                "EXTENSIONS (" +
                "test.CASEEXTRAINFO.caseextrainfo : VARCHAR(64), " +
                "test.CASEEXTRAINFO.extra : VARCHAR(64))");
        assertTrue(sql.contains("`caseextrainfo`.`caseextrainfo` AS `PK`"), sql);
        assertTrue(sql.contains("`caseextrainfo`.`extra` AS `col`"), sql);
        assertTrue(sql.contains("FROM `wrapped_caseextrainfo` AS `caseextrainfo`"), sql);
    }

    @Test
    void caseDifferentQualifierWithoutAliasFailsLikeRegularSql() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT " +
                        "`caseextrainfo`.`caseextrainfo` AS `PK`, " +
                        "`caseextrainfo`.`extra` AS `col` " +
                        "FROM `CASEEXTRAINFO` " +
                        "LIMIT 500 " +
                        "MAPPINGS (" +
                        "TABLE test.CASEEXTRAINFO TO wrapped_caseextrainfo, " +
                        "COLUMN test.CASEEXTRAINFO.caseextrainfo TO caseextrainfo, " +
                        "COLUMN test.CASEEXTRAINFO.extra TO extra) " +
                        "EXTENSIONS (" +
                        "test.CASEEXTRAINFO.caseextrainfo : VARCHAR(64), " +
                        "test.CASEEXTRAINFO.extra : VARCHAR(64))"));
        assertTrue(exception.getMessage().contains("Column '`caseextrainfo`.`caseextrainfo`' cannot be resolved"),
                exception.getMessage());
    }

    @Test
    void tableOnlyMappingSupportsMissingLogicalTableForCountStar() {
        String sql = run("REMAP LOGICAL SELECT COUNT(*) FROM logical_table " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.logical_table TO physical_table)");
        assertTrue(sql.contains("FROM `physical_table`"), sql);
    }

    @Test
    void missingLogicalColumnWithoutColumnMappingFails() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT logical_v1 FROM t0 " +
                        "LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0) " +
                        "EXTENSIONS (test.t0.logical_v1 : BIGINT)"));
        assertTrue(exception.getMessage().contains("has no REMAP LOGICAL column mapping"), exception.getMessage());
        assertNull(connectContext.getCelostarExtensions());
    }

    @Test
    void missingLogicalTableWithoutTableMappingFails() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("REMAP LOGICAL SELECT logical_v1 FROM logical_table " +
                        "LIMIT 500 " +
                        "MAPPINGS (COLUMN test.logical_table.logical_v1 TO physical_v1) " +
                        "EXTENSIONS (test.logical_table.logical_v1 : BIGINT)"));
        assertTrue(exception.getMessage().contains("has no REMAP LOGICAL table mapping"), exception.getMessage());
        assertNull(connectContext.getCelostarExtensions());
    }

    @Test
    void remapsPivotAggregateAndKeyColumns() {
        String sql = run("REMAP LOGICAL SELECT one, two FROM t0 " +
                "PIVOT (SUM(v1) FOR v2 IN (1 AS one, 2 AS two)) " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                "COLUMN test.t0.v1 TO physical_v1, COLUMN test.t0.v2 TO physical_v2)");
        assertTrue(sql.contains("FROM `physical_t0`"), sql);
        assertTrue(sql.contains("sum(`physical_v1`)") || sql.contains("sum(`physical_t0`.`physical_v1`)"), sql);
        assertTrue(sql.contains("FOR `physical_v2`") || sql.contains("FOR `physical_t0`.`physical_v2`"), sql);
        assertFalse(sql.contains("`v1`"), sql);
        assertFalse(sql.contains("`v2`"), sql);
    }

    @Test
    void remapsPivotWithMultipleAggregatesAndMultiColumnKey() {
        // A second aggregate exercises the comma-separator/alias branches between aggregate functions, and a
        // multi-column FOR clause with a multi-expr value tuple exercises the parenthesized-list rendering path.
        String sql = run("REMAP LOGICAL SELECT * FROM t0 " +
                "PIVOT (SUM(v1) AS sum_v1, MAX(v1) FOR (v2, v3) IN ((1, 1) AS one)) " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                "COLUMN test.t0.v1 TO physical_v1, COLUMN test.t0.v2 TO physical_v2, " +
                "COLUMN test.t0.v3 TO physical_v3)");
        assertTrue(sql.contains("FROM `physical_t0`"), sql);
        assertTrue(sql.contains("AS `sum_v1`") || sql.contains("AS sum_v1"), sql);
        assertTrue(sql.contains("(`physical_v2`, `physical_v3`)") ||
                sql.contains("(`physical_t0`.`physical_v2`, `physical_t0`.`physical_v3`)"), sql);
    }

    @Test
    void remapsViewRelationByAliasedName() throws Exception {
        starRocksAssert.withView("CREATE VIEW logical_view AS SELECT v1 FROM t0");
        try {
            String sql = run("REMAP LOGICAL SELECT v.v1 FROM logical_view AS v " +
                    "LIMIT 500 " +
                    "MAPPINGS (TABLE test.logical_view TO physical_view)");
            assertTrue(sql.contains("FROM `physical_view` AS `v`"), sql);
        } finally {
            starRocksAssert.dropView("logical_view");
        }
    }

    @Test
    void remapsSameNamedUnaliasedTablesFromDifferentDatabasesIndependently() throws Exception {
        starRocksAssert.createDatabaseIfNotExists("test2");
        starRocksAssert.useDatabase("test2");
        starRocksAssert.withTable("CREATE TABLE `dup_t` (\n" +
                "  `v1` bigint NULL\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 1\n" +
                "PROPERTIES('replication_num' = '1')");
        starRocksAssert.useDatabase("test");
        starRocksAssert.withTable("CREATE TABLE `dup_t` (\n" +
                "  `v1` bigint NULL\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 1\n" +
                "PROPERTIES('replication_num' = '1')");

        try {
            String sql = run("REMAP LOGICAL SELECT test.dup_t.v1, test2.dup_t.v1 " +
                    "FROM test.dup_t JOIN test2.dup_t ON test.dup_t.v1 = test2.dup_t.v1 " +
                    "LIMIT 500 " +
                    "MAPPINGS (TABLE test.dup_t TO physical_dup_1, TABLE test2.dup_t TO physical_dup_2, " +
                    "COLUMN test.dup_t.v1 TO phys_v1_1, COLUMN test2.dup_t.v1 TO phys_v1_2)");
            assertTrue(sql.contains("FROM `physical_dup_1`"), sql);
            assertTrue(sql.contains("JOIN `physical_dup_2`"), sql);
            assertTrue(sql.contains("`physical_dup_1`.`phys_v1_1`"), sql);
            assertTrue(sql.contains("`physical_dup_2`.`phys_v1_2`"), sql);
            assertFalse(sql.contains("`physical_dup_1`.`phys_v1_2`"), sql);
            assertFalse(sql.contains("`physical_dup_2`.`phys_v1_1`"), sql);
        } finally {
            starRocksAssert.dropTable("dup_t");
            starRocksAssert.useDatabase("test2");
            starRocksAssert.dropTable("dup_t");
            starRocksAssert.dropDatabase("test2");
            starRocksAssert.useDatabase("test");
        }
    }


    @Test
    void qualifiedTableBindingWinsOverSameNamedAliasInItsScope() throws Exception {
        starRocksAssert.createDatabaseIfNotExists("test2");
        starRocksAssert.useDatabase("test2");
        starRocksAssert.withTable("CREATE TABLE `dup_t` (\n" +
                "  `v1` bigint NULL\n" +
                ") ENGINE=OLAP\n" +
                "DUPLICATE KEY(`v1`)\n" +
                "DISTRIBUTED BY HASH(`v1`) BUCKETS 1\n" +
                "PROPERTIES('replication_num' = '1')");
        starRocksAssert.useDatabase("test");

        try {
            String sql = run("REMAP LOGICAL SELECT test2.dup_t.v1 " +
                    "FROM test2.dup_t CROSS JOIN test.t0 AS dup_t " +
                    "LIMIT 500 " +
                    "MAPPINGS (TABLE test2.dup_t TO physical_qualified, TABLE test.t0 TO physical_aliased, " +
                    "COLUMN test2.dup_t.v1 TO physical_qualified_v1, " +
                    "COLUMN test.t0.v1 TO physical_aliased_v1)");
            assertTrue(sql.contains("FROM `physical_qualified`"), sql);
            assertTrue(sql.contains("CROSS JOIN `physical_aliased` AS `dup_t`"), sql);
            assertTrue(sql.contains("`physical_qualified`.`physical_qualified_v1`"), sql);
            assertFalse(sql.contains("`dup_t`.`physical_qualified_v1`"), sql);
            assertFalse(sql.contains("physical_aliased_v1"), sql);
        } finally {
            starRocksAssert.useDatabase("test2");
            starRocksAssert.dropTable("dup_t");
            starRocksAssert.dropDatabase("test2");
            starRocksAssert.useDatabase("test");
        }
    }
}
