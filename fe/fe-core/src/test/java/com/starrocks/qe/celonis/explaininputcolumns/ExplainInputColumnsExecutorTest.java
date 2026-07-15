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

package com.starrocks.qe.celonis.explaininputcolumns;

import com.starrocks.catalog.Type;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests for ExplainInputColumnsExecutor output formatting and end-to-end query shapes.
 *
 * Database is "test"; available tables: t0(v1,v2,v3 bigint), t1(v4,v5,v6 bigint).
 */
class ExplainInputColumnsExecutorTest extends PlanTestBase {

    private String run(String sql) {
        return runResultSet(sql).getResultRows().stream()
                .map(row -> row.get(0))
                .collect(Collectors.joining("\n"));
    }

    private ShowResultSet runResultSet(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);
        return ExplainInputColumnsExecutor.execute((ExplainInputColumnsStmt) parsedStatement, connectContext);
    }

    @Test
    void twoColumnsFromSameTable() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1, v2 FROM t0");
        List<String> lines = Arrays.asList(output.split("\n"));
        assertTrue(lines.contains("default_catalog.test.t0.v1"), lines.toString());
        assertTrue(lines.contains("default_catalog.test.t0.v2"), lines.toString());
    }

    @Test
    void outputIsSortedLexicographically() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v3, v1, v2 FROM t0");
        List<String> lines = Arrays.asList(output.split("\n"));
        // All three present and sorted.
        assertTrue(lines.contains("default_catalog.test.t0.v1"), lines.toString());
        assertTrue(lines.contains("default_catalog.test.t0.v2"), lines.toString());
        assertTrue(lines.contains("default_catalog.test.t0.v3"), lines.toString());
        // The TreeSet guarantees lexicographic order.
        assertTrue(lines.indexOf("default_catalog.test.t0.v1") < lines.indexOf("default_catalog.test.t0.v2"));
        assertTrue(lines.indexOf("default_catalog.test.t0.v2") < lines.indexOf("default_catalog.test.t0.v3"));
    }

    @Test
    void joinProducesRowsFromBothSides() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1, v4 FROM t0 JOIN t1 ON t0.v1 = t1.v4");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
        assertTrue(output.contains("default_catalog.test.t1.v4"), output);
    }

    @Test
    void virtualColumnAppearsWhenReferenced() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertTrue(output.contains("default_catalog.test.t0.virt"), output);
    }

    @Test
    void aliasedVirtualColumnReportsRealTable() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT x.virt FROM t0 x " +
                "EXTENSIONS (test.t0.virt : BIGINT)");
        assertTrue(output.contains("default_catalog.test.t0.virt"), output);
        assertFalse(output.contains("default_catalog.test.x.virt"), output);
    }

    @Test
    void virtualColumnAbsentWhenNotReferenced() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertFalse(output.contains("virt"), "Unreferenced virtual column must not appear: " + output);
    }

    @Test
    void constantOnlyQueryProducesNoRows() {
        ShowResultSet resultSet = runResultSet("EXPLAIN INPUT COLUMNS SELECT 1");
        assertTrue(resultSet.getResultRows().isEmpty(), resultSet.getResultRows().toString());
    }

    @Test
    void selectStarIsRejected() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("EXPLAIN INPUT COLUMNS SELECT * FROM t0 EXTENSIONS (test.t0.virt : BIGINT)"));
        assertTrue(exception.getMessage().contains("SELECT * is not supported"), exception.getMessage());
    }

    @Test
    void selectStarInsideSubqueryIsRejected() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("EXPLAIN INPUT COLUMNS SELECT v1 FROM (SELECT * FROM t0) s " +
                        "EXTENSIONS (test.t0.virt : BIGINT)"));
        assertTrue(exception.getMessage().contains("SELECT * is not supported"), exception.getMessage());
    }

    @Test
    void columnOutsideSchemaAndExtensionsFailsBeforeFormatting() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> run("EXPLAIN INPUT COLUMNS SELECT missing_col FROM t0 EXTENSIONS (test.t0.virt : BIGINT)"));
        assertTrue(exception.getMessage().contains("cannot be resolved"), exception.getMessage());
    }

    @Test
    void realAndVirtualColumnCoexistInOutput() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertTrue(output.contains("default_catalog.test.t0.v1"), "real column must appear: " + output);
        assertTrue(output.contains("default_catalog.test.t0.virt"), "virtual column must appear: " + output);
        // Only these two lines should be produced for t0.
        long lineCount = Arrays.stream(output.split("\n"))
                .filter(line -> line.startsWith("default_catalog.test.t0."))
                .count();
        assertEquals(2, lineCount, "expected exactly two t0 lines: " + output);
    }

    @Test
    void emptyExtensionListWorks() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0 EXTENSIONS ()");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
    }

    @Test
    void noExtensionClauseWorks() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
    }

    @Test
    void virtualTableColumnAppearsWhenReferenced() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT virt FROM vtab EXTENSIONS (test.vtab.virt : BIGINT)");
        assertTrue(output.contains("default_catalog.test.vtab.virt"), output);
    }

    @Test
    void joinExistingAndVirtualTableProducesRowsFromBothSides() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 JOIN vtab ON t0.v1 = vtab.virt " +
                "EXTENSIONS (test.vtab.virt : BIGINT)");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
        assertTrue(output.contains("default_catalog.test.vtab.virt"), output);
    }

    @Test
    void unreferencedVirtualTableDoesNotAppear() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0 EXTENSIONS (test.vtab.virt : BIGINT)");
        assertFalse(output.contains("vtab"), "Unreferenced virtual table must not appear: " + output);
    }

    @Test
    void cteColumnsResolveToBaseTable() {
        // CTE aliases should be transparent; the underlying table reference is what matters.
        String output = run("EXPLAIN INPUT COLUMNS WITH cte AS (SELECT v1 FROM t0) SELECT v1 FROM cte");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
    }

    @Test
        void cteWithUnusedProjectedColumnsReportsBaseTableColumns() {
        String output = run("EXPLAIN INPUT COLUMNS WITH stats AS (" +
                "SELECT v1, v2, v3 FROM t0" +
                ") SELECT v1, v2 FROM stats");
        assertEquals(List.of(
                "default_catalog.test.t0.v1",
                "default_catalog.test.t0.v2",
                "default_catalog.test.t0.v3"), Arrays.asList(output.split("\n")));
    }

    @Test
    void subqueryResolvesToBaseTable() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v1 FROM (SELECT v1 FROM t0) sub");
        assertTrue(output.contains("default_catalog.test.t0.v1"), output);
    }

    @Test
    void aliasedSubqueryRenamedColumnResolvesToBaseColumn() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT wrapped.renamed_v1 " +
                "FROM (SELECT base_table.v1 AS renamed_v1 FROM t0 base_table) wrapped");
        assertEquals(List.of("default_catalog.test.t0.v1"), Arrays.asList(output.split("\n")));
    }

    @Test
    void explainInputColumnsExtensionsAreStatementScoped() {
        CelostarSchemaExtension previousSchemaExtension = new CelostarSchemaExtension();
        previousSchemaExtension.add(new ExtensionTableKey("default_catalog", "test", "t0"),
                "previous_virtual", Type.BIGINT);
        CelostarExtensionSet previousExtensions =
                CelostarExtensionSet.ofSchemaExtension(previousSchemaExtension);
        CelostarExtensionSet originalExtensions = connectContext.getCelostarExtensions();
        connectContext.setCelostarExtensions(previousExtensions);

        try {
            String output = run("EXPLAIN INPUT COLUMNS SELECT statement_virtual FROM t0 " +
                    "EXTENSIONS (test.t0.statement_virtual : BIGINT)");
            assertEquals("default_catalog.test.t0.statement_virtual", output);
            assertSame(previousExtensions, connectContext.getCelostarExtensions());

            StatementBase previousExtensionQuery = SqlParser.parse("SELECT previous_virtual FROM t0",
                    connectContext.getSessionVariable()).get(0);
            Analyzer.analyze(previousExtensionQuery, connectContext);

            StatementBase leakedExtensionQuery = SqlParser.parse("SELECT statement_virtual FROM t0",
                    connectContext.getSessionVariable()).get(0);
            assertThrows(SemanticException.class, () -> Analyzer.analyze(leakedExtensionQuery, connectContext));
        } finally {
            connectContext.setCelostarExtensions(originalExtensions);
        }
    }
}
