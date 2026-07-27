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

package com.starrocks.sql.analyzer.celonis.validate;

import com.starrocks.common.Config;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests that VALIDATE analysis surfaces genuine parse/analysis problems as errors (as opposed to whitelist violations,
 * which the executor reports as a result set), and that declared extensions make external columns resolvable.
 *
 * <p>Database is "test"; tables t0(v1,v2,v3 bigint).
 */
class ValidateAnalyzerTest extends PlanTestBase {

    @AfterEach
    void resetConfig() {
        Config.validate_max_limit = 0;
        Config.validate_allowed_functions = new String[0];
    }

    private void analyze(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);
    }

    /**
     * The raw-AST safety pass runs before ordinary analysis, so it must find an unsafe nested query regardless of
     * where that query appears in the enclosing statement. Every query below exercises a distinct traversal path:
     * select-list expression, WHERE, HAVING, JOIN ON, derived relation, CTE, set-operation branch, ORDER BY, and
     * GROUP BY. The last two are rejected later by ordinary subquery analysis, but checking them here proves that the
     * pre-analysis pass still visits their raw expression nodes first.
     */
    private void assertUnsafeNestedQueryIsRejectedInEveryPlacement(String nestedQuery, String expectedMessage) {
        List<String> queries = List.of(
                "VALIDATE SELECT (" + nestedQuery + ") FROM t0",
                "VALIDATE SELECT v1 FROM t0 WHERE EXISTS (" + nestedQuery + ")",
                "VALIDATE SELECT v1 FROM t0 GROUP BY v1 HAVING EXISTS (" + nestedQuery + ")",
                "VALIDATE SELECT t0.v1 FROM t0 JOIN t1 ON EXISTS (" + nestedQuery + ")",
                "VALIDATE SELECT 1 FROM (" + nestedQuery + ") nested_query",
                "VALIDATE WITH nested_query AS (" + nestedQuery + ") SELECT 1 FROM nested_query",
                "VALIDATE SELECT 1 FROM t0 UNION ALL SELECT 1 FROM (" + nestedQuery + ") nested_query",
                "VALIDATE SELECT v1 FROM t0 ORDER BY (" + nestedQuery + ")",
                "VALIDATE SELECT 1 FROM t0 GROUP BY (" + nestedQuery + ")");

        for (String query : queries) {
            SemanticException exception = assertThrows(SemanticException.class, () -> analyze(query), query);
            assertTrue(exception.getMessage().contains(expectedMessage),
                    () -> "Expected raw-AST rejection for query: " + query + ", but got: " + exception.getMessage());
        }
    }

    @Test
    void unknownColumnThrows() {
        assertThrows(SemanticException.class, () -> analyze("VALIDATE SELECT missing_col FROM t0"));
        assertNull(connectContext.getCelostarExtensions());
    }

    @Test
    void externalColumnResolvesViaExtensions() {
        // Column defined elsewhere resolves during analysis; no exception.
        analyze("VALIDATE SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertNull(connectContext.getCelostarExtensions());
    }

    @Test
    void extensionOnExistingColumnThrows() {
        assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT v1 FROM t0 EXTENSIONS (test.t0.v1 : BIGINT)"));
    }

    @Test
    void queryScopeHintThrows() {
        // AstBuilder attaches query-scope hints to the outermost statement (ValidateStmt here), not to
        // getQueryStmt() — VALIDATE must reject them outright rather than let them slip past the whitelist.
        assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT /*+ SET_VAR(query_timeout=1) */ v1 FROM t0"));
    }

    @Test
    void selectStarThrows() {
        // * expands to whatever columns the table happens to have -- including virtual/extension columns installed
        // only for this VALIDATE -- so its meaning is table-shape-dependent; require explicit columns instead.
        assertThrows(SemanticException.class, () -> analyze("VALIDATE SELECT * FROM t0"));
    }

    @Test
    void nestedSelectStarThrowsInEverySubqueryPlacement() {
        assertUnsafeNestedQueryIsRejectedInEveryPlacement("SELECT * FROM t1", "SELECT * is not supported in VALIDATE");
    }

    @Test
    void nestedFilesThrowsInEverySubqueryPlacement() {
        assertUnsafeNestedQueryIsRejectedInEveryPlacement(
                "SELECT count(*) FROM FILES(\"format\" = \"parquet\", \"path\" = \"s3://bucket/x\")",
                "VALIDATE does not allow table function: files");
    }

    private static final String NESTED_FILES =
            "(SELECT count(*) FROM FILES(\"format\" = \"parquet\", \"path\" = \"s3://bucket/x\"))";

    @Test
    void nestedFilesInsideTableFunctionArgumentThrows() {
        // RawAstSafetyVisitor.visitTableFunction did not recurse into raw function-call arguments, so a FILES()
        // subquery smuggled into another (whitelisted) table function's argument list reached analysis unnoticed.
        Config.validate_allowed_functions = new String[] {"generate_series"};
        SemanticException exception = assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT 1 FROM generate_series(1, " + NESTED_FILES + ")"));
        assertTrue(exception.getMessage().contains("VALIDATE does not allow table function: files"),
                exception.getMessage());
    }

    @Test
    void nestedFilesInsideValuesRowThrows() {
        // The stock AstTraverser treats ValuesRelation as a leaf, so a FILES() subquery inside a VALUES row was
        // never visited by the raw-AST pass.
        SemanticException exception = assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT a FROM (VALUES (1, " + NESTED_FILES + "), (2, 2)) t(a, b)"));
        assertTrue(exception.getMessage().contains("VALIDATE does not allow table function: files"),
                exception.getMessage());
    }

    @Test
    void nestedFilesInsidePivotSourceThrows() {
        // The stock AstTraverser treats PivotRelation as a leaf, so the relation being pivoted was never visited by
        // the raw-AST pass; FILES() used directly as the pivot source reached analysis unnoticed.
        SemanticException exception = assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT 1 FROM FILES(\"format\" = \"parquet\", \"path\" = \"s3://bucket/x\") " +
                        "PIVOT (sum(v1) FOR v2 IN (1, 2))"));
        assertTrue(exception.getMessage().contains("VALIDATE does not allow table function: files"),
                exception.getMessage());
    }

    @Test
    void limitDisabledByDefaultAllowsMissingLimit() {
        // validate_max_limit defaults to 0 (disabled); a query without a LIMIT still analyzes cleanly.
        assertDoesNotThrow(() -> analyze("VALIDATE SELECT v1 FROM t0"));
    }

    @Test
    void limitWithinMaxIsValid() {
        Config.validate_max_limit = 500;
        assertDoesNotThrow(() -> analyze("VALIDATE SELECT v1 FROM t0 LIMIT 100"));
    }

    @Test
    void limitEqualToMaxIsValid() {
        Config.validate_max_limit = 500;
        assertDoesNotThrow(() -> analyze("VALIDATE SELECT v1 FROM t0 LIMIT 500"));
    }

    @Test
    void limitExceedingMaxThrows() {
        Config.validate_max_limit = 500;
        assertThrows(SemanticException.class, () -> analyze("VALIDATE SELECT v1 FROM t0 LIMIT 1000"));
    }

    @Test
    void missingLimitWhenMaxConfiguredThrows() {
        Config.validate_max_limit = 500;
        assertThrows(SemanticException.class, () -> analyze("VALIDATE SELECT v1 FROM t0"));
    }

    @Test
    void unionOuterLimitIsChecked() {
        // The outer LIMIT applies to the whole UNION, not to either branch individually.
        Config.validate_max_limit = 500;
        assertThrows(SemanticException.class,
                () -> analyze("VALIDATE SELECT v1 FROM t0 UNION SELECT v4 FROM t1 LIMIT 1000"));
        assertDoesNotThrow(
                () -> analyze("VALIDATE SELECT v1 FROM t0 UNION SELECT v4 FROM t1 LIMIT 100"));
    }
}
