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

import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Type;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.qe.SqlModeHelper;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.Authorizer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.SubqueryRelation;
import com.starrocks.sql.ast.TableRelation;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;
import org.mockito.MockedStatic;
import org.mockito.Mockito;

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
        return runRows(sql).stream()
                .map(row -> row.get(0))
                .collect(Collectors.joining("\n"));
    }

    private List<List<String>> runRows(String sql) {
        return runResultSet(sql).getResultRows();
    }

    private ShowResultSet runResultSet(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);
        return ExplainInputColumnsExecutor.execute((ExplainInputColumnsStmt) parsedStatement, connectContext);
    }

    @Test
    void twoColumnsFromSameTable() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2")),
                runRows("EXPLAIN INPUT COLUMNS SELECT v1, v2 FROM t0"));
    }

    @Test
    void columnCaseVariantsAreDeduplicatedUsingCatalogSpelling() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1")),
                runRows("EXPLAIN INPUT COLUMNS SELECT v1, V1 FROM t0"));
    }

    @Test
    void virtualColumnCaseVariantsUseExtensionSpelling() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.virt")),
                runRows("EXPLAIN INPUT COLUMNS SELECT VIRT, virt FROM t0 " +
                        "EXTENSIONS (test.t0.virt : BIGINT)"));
    }

    @Test
    void resultSchemaRemainsBackwardCompatible() {
        ShowResultSet resultSet = runResultSet("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0");
        assertEquals(1, resultSet.getMetaData().getColumnCount());
        assertEquals("Input Column", resultSet.getMetaData().getColumn(0).getName());
    }

    @Test
    void outputIsSortedLexicographically() {
        String output = run("EXPLAIN INPUT COLUMNS SELECT v3, v1, v2 FROM t0");
        List<String> lines = Arrays.asList(output.split("\n"));
        // All three present and sorted.
        assertTrue(lines.contains("default_catalog.test.t0.v1"), lines.toString());
        assertTrue(lines.contains("default_catalog.test.t0.v2"), lines.toString());
        assertTrue(lines.contains("default_catalog.test.t0.v3"), lines.toString());
        // Output references are kept in lexicographic order.
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
    void countStarReportsTableReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM t0"));
    }

    @Test
    void constantFromTableReportsTableReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT 1 FROM t0"));
    }

    @Test
    void countStarFromVirtualTableReportsTableReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.vtab.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM vtab " +
                        "EXTENSIONS (test.vtab.virt : BIGINT)"));
    }

    @Test
    void markerNamedColumnIsRejectedAsReserved() {
        List<List<String>> tableReference = runRows(
                "EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM vtab " +
                        "EXTENSIONS (test.vtab.__STARROCKS_TABLE_REF__ : BIGINT)");

        assertEquals(List.of(
                List.of("default_catalog.test.vtab.__STARROCKS_TABLE_REF__")), tableReference);
        SemanticException exception = assertThrows(SemanticException.class,
                () -> runRows("EXPLAIN INPUT COLUMNS SELECT __STARROCKS_TABLE_REF__ FROM vtab " +
                        "EXTENSIONS (test.vtab.__STARROCKS_TABLE_REF__ : BIGINT)"));
        assertTrue(exception.getDetailMsg().contains("reserved"), exception.getDetailMsg());
    }

    @Test
    void similarlyNamedColumnIsNotReserved() {
        assertEquals(List.of(
                List.of("default_catalog.test.vtab.__STARROCKS_TABLE_REF__value")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT __STARROCKS_TABLE_REF__value FROM vtab " +
                        "EXTENSIONS (test.vtab.__STARROCKS_TABLE_REF__value : BIGINT)"));
    }

    @Test
    void countStarThroughCteReportsBaseTableReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "WITH cte AS (SELECT COUNT(*) FROM t0) SELECT COUNT(*) FROM cte"));
    }

    @Test
    void countStarThroughDerivedTableReportsBaseTableReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT COUNT(*) FROM (SELECT COUNT(*) FROM t0) nested"));
    }

    @Test
    void multipleTableOnlyReferencesAreReported() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__"),
                List.of("default_catalog.test.t1.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM t0 CROSS JOIN t1"));
    }

    @Test
    void countStarFromViewReportsOnlyViewReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.tview.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM tview"));
    }

    @Test
    void countStarSubqueryReportsTableReferenceAlongsideInputColumn() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t1.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS SELECT COUNT(t0.v1) FROM t0 " +
                        "CROSS JOIN (SELECT COUNT(*) FROM t1) counts"));
    }

    @Test
    void reusedAliasAcrossUnionArmsResolvesWithinEachArm() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t1.v4")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a UNION ALL SELECT a.v4 FROM t1 a"));
    }

    @Test
    void nestedAliasShadowsOuterAliasWithoutOverwritingIt() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t1.v4")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1, (SELECT MAX(a.v4) FROM t1 a) FROM t0 a"));
    }

    @Test
    void correlatedSubqueryResolvesOuterAndInnerAliases() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS " +
                        "(SELECT 1 FROM t1 b WHERE b.v4 = a.v2)"));
    }

    @Test
    void innerAliasWithoutColumnDoesNotStealJoinCorrelation() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (" +
                        "SELECT 1 FROM t1 a " +
                        "JOIN t1 b ON b.v4 = a.v2)"));
    }

    @Test
    void innerSelectAliasWithoutColumnFallsBackToOuterScope() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (" +
                        "SELECT a.v2 FROM t1 a)"));
    }

    @Test
    void nestedJoinCannotStealCorrelatedAliasFromLaterOperand() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (" +
                        "SELECT 1 FROM t1 b " +
                        "JOIN t1 c ON b.v4 = a.v2 " +
                        "CROSS JOIN t1 a)"));
    }

    @Test
    void nonLateralValuesCannotSeeSiblingAlias() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (" +
                        "SELECT 1 FROM t1 a " +
                        "CROSS JOIN (VALUES (a.v2)) vals(x))"));
    }

    @Test
    void policyGeneratedAliasBlocksFallbackToOuterTable() {
        StatementBase parsedStatement = SqlParser.parse(
                "EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (SELECT a.v4, a.v2 FROM t1 a)",
                connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);

        // Simulate a policy helper after name resolution; its reused alias must remain a scope barrier.
        new AstTraverser<Void, Void>() {
            @Override
            public Void visitTable(TableRelation node, Void context) {
                if ("t1".equals(node.getName().getTbl())) {
                    node.setCreateByPolicyRewritten(true);
                }
                return null;
            }
        }.visit(parsedStatement);

        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2")),
                ExplainInputColumnsExecutor.execute(
                        (ExplainInputColumnsStmt) parsedStatement, connectContext).getResultRows());
    }

    /**
     * Verifies that a real policy rewrite does not turn its generated projection into reported inputs.
     */
    @Test
    void policyRewriteSkipsSynthesizedProjectionColumns() {
        TableName tableName = new TableName("default_catalog", "test", "t0");
        try (MockedStatic<Authorizer> authorizer = Mockito.mockStatic(Authorizer.class)) {
            authorizer.when(() -> Authorizer.getRowAccessPolicy(Mockito.any(), Mockito.eq(tableName)))
                    .thenAnswer(invocation -> SqlParser.parseSqlToExpr("v1 > 0", SqlModeHelper.MODE_DEFAULT));

            assertEquals(List.of(
                    List.of("default_catalog.test.t0.__STARROCKS_TABLE_REF__")),
                    runRowsWithPolicyRewrite("EXPLAIN INPUT COLUMNS SELECT COUNT(*) FROM t0"));
            assertEquals(List.of(
                    List.of("default_catalog.test.t0.v1")),
                    runRowsWithPolicyRewrite("EXPLAIN INPUT COLUMNS SELECT v1 FROM t0"));
        }
    }

    /**
     * Analyzes and executes a statement after enabling the normal policy-rewrite path for its table.
     */
    private List<List<String>> runRowsWithPolicyRewrite(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        new AstTraverser<Void, Void>() {
            @Override
            public Void visitTable(TableRelation node, Void context) {
                node.setNeedRewrittenByPolicy(true);
                return null;
            }
        }.visit(parsedStatement);
        Analyzer.analyze(parsedStatement, connectContext);

        ExplainInputColumnsStmt explainStatement = (ExplainInputColumnsStmt) parsedStatement;
        SelectRelation outerSelect = (SelectRelation) explainStatement.getQueryStmt().getQueryRelation();
        assertTrue(outerSelect.getRelation() instanceof SubqueryRelation);
        SubqueryRelation policySubquery = (SubqueryRelation) outerSelect.getRelation();
        assertTrue(policySubquery.getQueryStatement().getQueryRelation().isCreateByPolicyRewritten());
        return ExplainInputColumnsExecutor.execute(explainStatement, connectContext).getResultRows();
    }

    @Test
    void correlatedCteUsesItsDeclarationScope() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.__STARROCKS_TABLE_REF__")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT a.v1 FROM t0 a WHERE EXISTS (" +
                        "WITH c AS (SELECT a.v2) " +
                        "SELECT 1 FROM t1 a CROSS JOIN c)"));
    }

    @Test
    void pivotReportsItsPhysicalInputColumns() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t0.v3")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT 1 FROM t0 PIVOT (COUNT(*) FOR v2 IN (1, 2))"));
    }

    @Test
    void pivotReportsEveryAggregateArgument() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t0.v3")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT 1 FROM t0 PIVOT (COUNT(DISTINCT v1, v2) FOR v3 IN (1, 2))"));
    }

    @Test
    void pivotOutputSeparatesPassThroughAndGeneratedFields() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t0.v3")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT v1, one FROM t0 " +
                        "PIVOT (SUM(v3) FOR v2 IN (1 AS one))"));
    }

    @Test
    void tableFunctionArgumentReportsItsPhysicalInputColumn() {
        assertEquals(List.of(
                List.of("default_catalog.test.tarray.v3")),
                runRows("EXPLAIN INPUT COLUMNS SELECT 1 FROM tarray, UNNEST(v3)"));
    }

    @Test
    void normalizedTableFunctionWithoutPhysicalInputsReturnsNoReferences() {
        assertEquals(List.of(),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT 1 FROM TABLE(UNNEST(ARRAY<INT>[1, 2]))"));
    }

    @Test
    void leftSemiJoinExposesOnlyItsRetainedSideToTheFollowingJoin() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t0.v3"),
                List.of("default_catalog.test.t1.v5"),
                List.of("default_catalog.test.t2.v7"),
                List.of("default_catalog.test.t2.v8")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT kept.v1, tail.v7 " +
                        "FROM t0 kept LEFT SEMI JOIN t1 filtered ON kept.v2 = filtered.v5 " +
                        "JOIN t2 tail ON kept.v3 = tail.v8"));
    }

    @Test
    void rightAntiJoinExposesOnlyItsRetainedSideToTheFollowingJoin() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4"),
                List.of("default_catalog.test.t1.v5"),
                List.of("default_catalog.test.t1.v6"),
                List.of("default_catalog.test.t2.v7"),
                List.of("default_catalog.test.t2.v8")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT kept.v4, tail.v7 " +
                        "FROM t0 filtered RIGHT ANTI JOIN t1 kept ON filtered.v2 = kept.v5 " +
                        "JOIN t2 tail ON kept.v6 = tail.v8"));
    }

    @Test
    void leftOuterJoinPreservesPhysicalOwnersAcrossNullableFieldCopies() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4"),
                List.of("default_catalog.test.t1.v5")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT l.v1, r.v4 FROM t0 l " +
                        "LEFT OUTER JOIN t1 r ON l.v2 = r.v5"));
    }

    @Test
    void rightOuterJoinPreservesPhysicalOwnersAcrossNullableFieldCopies() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4"),
                List.of("default_catalog.test.t1.v5")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT l.v1, r.v4 FROM t0 l " +
                        "RIGHT OUTER JOIN t1 r ON l.v2 = r.v5"));
    }

    @Test
    void fullOuterJoinPreservesPhysicalOwnersAcrossNullableFieldCopies() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v4"),
                List.of("default_catalog.test.t1.v5")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT l.v1, r.v4 FROM t0 l " +
                        "FULL OUTER JOIN t1 r ON l.v2 = r.v5"));
    }

    @Test
    void joinSkewHintReportsItsExplicitColumn() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2"),
                List.of("default_catalog.test.t1.v5")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT 1 FROM t0 " +
                        "JOIN [skew|t0.v1(1, 2)] t1 ON t0.v2 = t1.v5"));
    }

    @Test
    void windowSkewHintReportsItsExplicitColumn() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1"),
                List.of("default_catalog.test.t0.v2")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT SUM(v2) OVER ([skew|v1(1)] PARTITION BY v2) FROM t0"));
    }

    @Test
    void tableReferenceIsSuppressedWhenTheSameTableHasAColumnReference() {
        assertEquals(List.of(
                List.of("default_catalog.test.t0.v1")),
                runRows("EXPLAIN INPUT COLUMNS " +
                        "SELECT t0.v1 FROM t0 CROSS JOIN (SELECT COUNT(*) FROM t0) counts"));
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
