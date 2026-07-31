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

package com.starrocks.sql.analyzer.celonis.explaininputcolumns;

import com.starrocks.catalog.Type;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.Authorizer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Integration tests for ExplainInputColumnsAnalyzer.
 *
 * Database is "test"; tables available: t0(v1,v2,v3 bigint), t1(v4,v5,v6 bigint).
 */
class ExplainInputColumnsAnalyzerTest extends PlanTestBase {

    private static ExplainInputColumnsStmt parse(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        return (ExplainInputColumnsStmt) parsedStatement;
    }

    private static ExplainInputColumnsStmt analyzeOk(String sql) {
        ExplainInputColumnsStmt statement = parse(sql);
        assertDoesNotThrow(() -> Analyzer.analyze(statement, connectContext));
        return statement;
    }

    private static void analyzeFail(String sql, String messageFragment) {
        ExplainInputColumnsStmt statement = parse(sql);
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(statement, connectContext));
        assertTrue(exception.getMessage().contains(messageFragment),
                "Expected '" + messageFragment + "' in: " + exception.getMessage());
    }

    @Test
    void virtualColumnResolves() {
        // A plain extension column resolves and the query analyzes without error.
        ExplainInputColumnsStmt statement =
                analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertEquals(Type.BIGINT, statement.getQueryStmt().getQueryRelation().getOutputExpression().get(0).getType());
    }

    @Test
    void virtualColumnResolvesThroughTableAlias() {
        ExplainInputColumnsStmt statement =
                analyzeOk("EXPLAIN INPUT COLUMNS SELECT x.virt FROM t0 AS x " +
                        "EXTENSIONS (test.t0.virt : BIGINT)");
        assertEquals(Type.BIGINT, statement.getQueryStmt().getQueryRelation().getOutputExpression().get(0).getType());
    }

    @Test
    void realColumnResolvesWhenLogicalTableCaseComesFromExtension() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1 FROM T0 EXTENSIONS (test.T0.virt : BIGINT)");
    }

    @Test
    void virtualColumnArithmetic() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt + 1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnFunction() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT LENGTH(virt) FROM t0 EXTENSIONS (test.t0.virt : VARCHAR(20))");
    }

    @Test
    void virtualColumnSubscript() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt[0] FROM t0 EXTENSIONS (test.t0.virt : ARRAY<BIGINT>)");
    }

    @Test
    void virtualTableColumnResolves() {
        ExplainInputColumnsStmt statement = analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM vtab " +
                "EXTENSIONS (test.vtab.virt : BIGINT)");
        assertEquals(Type.BIGINT, statement.getQueryStmt().getQueryRelation().getOutputExpression().get(0).getType());
    }

    @Test
    void virtualTableCanJoinExistingTable() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 JOIN vtab ON t0.v1 = vtab.virt " +
                "EXTENSIONS (test.vtab.virt : BIGINT)");
    }

    @Test
    void virtualTableDoesNotRequireCatalogPrivilegeObject() {
        ExplainInputColumnsStmt statement = analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM vtab " +
                "EXTENSIONS (test.vtab.virt : BIGINT)");
        assertDoesNotThrow(() -> Authorizer.check(statement, connectContext));
    }

    @Test
    void collisionWithRealColumn() {
        // v1 is a real column on t0, so the extension must be rejected.
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.v1 : BIGINT)",
                "already exists on table");
    }

    @Test
    void extensionTableMayBeVirtual() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM nope EXTENSIONS (test.nope.virt : BIGINT)");
    }

    @Test
    void missingColumnOutsideSchemaAndExtensionsFails() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT missing_col FROM t0 EXTENSIONS (test.t0.virt : BIGINT)",
                "cannot be resolved");
    }

    @Test
    void missingTableOutsideExtensionsFails() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT v1 FROM missing_table EXTENSIONS (test.t0.virt : BIGINT)",
                "Unknown table");
    }

    @Test
    void missingExtensionDatabaseFails() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT virt FROM nope EXTENSIONS (missing_db.nope.virt : BIGINT)",
                "unknown database");
    }

    @Test
    void missingExtensionCatalogFails() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT virt FROM nope EXTENSIONS (missing_catalog.test.nope.virt : BIGINT)",
                "unknown catalog");
    }

    @Test
    void duplicateExtension() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT, test.t0.virt : BIGINT)",
                "Duplicate schema extension");
    }

    @Test
    void virtualColumnIsNull() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt IS NULL FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnIsNotNull() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt IS NOT NULL FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnLike() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt LIKE '%foo%' FROM t0 EXTENSIONS (test.t0.virt : VARCHAR(20))");
    }

    @Test
    void virtualColumnBetween() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt BETWEEN 1 AND 10 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnIn() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt IN (1, 2, 3) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnCast() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT CAST(virt AS BIGINT) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnCoalesce() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT COALESCE(virt, 0) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnIf() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT IF(virt IS NULL, 0, v1) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnMultiplyWithReal() {
        // Arithmetic between a typed virtual column and a real column.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt * v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnWhere() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 WHERE virt > 0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnOrderBy() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 ORDER BY virt EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnGroupBy() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt, COUNT(*) FROM t0 GROUP BY virt EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnCountAggregate() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT COUNT(virt) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnMaxAggregate() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT MAX(virt) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnDistinct() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT DISTINCT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void multipleVirtualColumns() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT a, b FROM t0 EXTENSIONS (test.t0.a : BIGINT, test.t0.b : BIGINT)");
    }

    @Test
    void multipleVirtualColumnsArithmetic() {
        // Two typed virtual columns in arithmetic with each other.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT a + b FROM t0 EXTENSIONS (test.t0.a : BIGINT, test.t0.b : BIGINT)");
    }

    @Test
    void virtPlusReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt + v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void realPlusVirt() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1 + virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtMinusReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt - v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void realMinusVirt() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1 - virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtDivideReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt / v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtModuloReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt % v2 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void chainedRealPlusVirt() {
        // Three-term chain: two real columns plus one virtual.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1 + v2 + virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtEqualsReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt = v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtGreaterThanReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt > v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtLessThanOrEqualReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt <= v2 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void whereVirtComparedToReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 WHERE virt > v1 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void whereCompoundRealAndVirt() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 WHERE v1 > 0 AND virt IS NOT NULL " +
                "EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void groupByRealAggregateVirt() {
        // Group by a real column while aggregating over the virtual column.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, MAX(virt) FROM t0 GROUP BY v1 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void aggregateOverRealPlusVirt() {
        // Arithmetic inside an aggregate function.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT SUM(v1 + virt) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void orderByRealThenVirt() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 ORDER BY v1, virt EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void orderByVirtThenReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, virt FROM t0 ORDER BY virt, v1 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void coalesceVirtWithReal() {
        // Use a real column as the fallback for a virtual one.
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT COALESCE(virt, v1) FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void caseWhenVirtVsReal() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT CASE WHEN virt > v1 THEN v2 ELSE virt END FROM t0 " +
                "EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void selectRealAndVirtTogether() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT v1, v2, virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
    }

    @Test
    void virtualColumnChainedSubscript() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt[0][1] FROM t0 EXTENSIONS (test.t0.virt : ARRAY<ARRAY<BIGINT>>)");
    }

    @Test
    void virtualColumnMixedChain() {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt[0].field FROM t0 EXTENSIONS " +
                "(test.t0.virt : ARRAY<STRUCT<field BIGINT>>)");
    }

    @Test
    void arrowExprNotSupportedOnVirtualColumn() {
        // The extension declares BIGINT, so JSON arrow access must fail normally.
        ExplainInputColumnsStmt statement =
                parse("EXPLAIN INPUT COLUMNS SELECT virt->'key' FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(statement, connectContext));
        assertTrue(exception.getMessage().contains("json column"),
                "Expected json-column error for arrow on virtual column: " + exception.getMessage());
    }

    @Test
    void nullSubscriptStillFailsOutsideExplainInputColumns() {
        // SELECT NULL[0] must still throw outside EXPLAIN INPUT COLUMNS.
        assertNull(connectContext.getCelostarExtensions(), "celostarExtensions must be null for this test");
        SemanticException exception = assertThrows(SemanticException.class, () -> {
            StatementBase parsedStatement =
                    SqlParser.parse("SELECT NULL[0]", connectContext.getSessionVariable()).get(0);
            Analyzer.analyze(parsedStatement, connectContext);
        });
        assertNotNull(exception);
    }

    @Test
    void nullSubfieldStillFailsOutsideExplainInputColumns() {
        assertNull(connectContext.getCelostarExtensions());
        assertThrows(SemanticException.class, () -> {
            StatementBase parsedStatement =
                    SqlParser.parse("SELECT NULL.foo", connectContext.getSessionVariable()).get(0);
            Analyzer.analyze(parsedStatement, connectContext);
        });
    }

    @Test
    void contextClearedAfterNormalAnalysis() throws Exception {
        analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertNull(connectContext.getCelostarExtensions(),
                "celostarExtensions must be null after analysis completes");
    }

    @Test
    void contextClearedAfterFailedAnalysis() {
        // Even when analysis fails, context must be cleaned up.
        try {
            ExplainInputColumnsStmt statement = parse("EXPLAIN INPUT COLUMNS SELECT virt FROM nope " +
                    "EXTENSIONS (missing_db.nope.virt : BIGINT)");
            Analyzer.analyze(statement, connectContext);
        } catch (SemanticException ignored) {
        }
        assertNull(connectContext.getCelostarExtensions(),
                "celostarExtensions must be null even when analysis fails");
    }

    @Test
    void nestedSelectStarInSelectListThrows() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT (SELECT * FROM t0 LIMIT 1) FROM t1",
                "SELECT * is not supported in EXPLAIN INPUT COLUMNS");
    }

    @Test
    void nestedSelectStarInGroupByThrows() {
        analyzeFail(
                "EXPLAIN INPUT COLUMNS SELECT 1 FROM t1 GROUP BY (SELECT * FROM t0 LIMIT 1)",
                "SELECT * is not supported in EXPLAIN INPUT COLUMNS");
    }

    @Test
    void nestingRestoresPriorState() {
        // Simulate a scenario where celostarExtensions was already set on the context before
        // ExplainInputColumnsAnalyzer.analyze is called.
        // The inner call must restore the prior state on exit.
        CelostarSchemaExtension existingSchemaExtension = new CelostarSchemaExtension();
        connectContext.setCelostarExtensions(CelostarExtensionSet.ofSchemaExtension(existingSchemaExtension));
        try {
            analyzeOk("EXPLAIN INPUT COLUMNS SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
            // After inner analyze() completes, prior state must be restored.
            assertNotNull(connectContext.getCelostarExtensions(),
                    "Prior celostarExtensions must be restored after nested analyze()");
        } finally {
            connectContext.setCelostarExtensions(null);
        }
    }
}
