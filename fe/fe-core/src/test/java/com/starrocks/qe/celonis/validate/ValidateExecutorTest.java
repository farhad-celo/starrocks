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

package com.starrocks.qe.celonis.validate;

import com.starrocks.common.Config;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.validate.ValidateStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests for the VALIDATE statement whitelist gate.
 *
 * <p>Database is "test"; available tables: t0(v1,v2,v3 bigint), t1(v4,v5,v6 bigint), plus a virtual table "vtab".
 */
class ValidateExecutorTest extends PlanTestBase {

    @AfterEach
    void resetConfig() {
        Config.validate_allowed_functions = new String[] {};
    }

    private List<List<String>> run(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(parsedStatement, connectContext);
        ShowResultSet resultSet = ValidateExecutor.execute((ValidateStmt) parsedStatement);
        return resultSet.getResultRows();
    }

    private boolean isValid(List<List<String>> rows) {
        return rows.size() == 1 && rows.get(0).equals(List.of("status", "VALID"));
    }

    private boolean hasViolation(List<List<String>> rows, String kind, String detail) {
        return rows.stream().anyMatch(row -> row.get(0).equals(kind) && row.get(1).equalsIgnoreCase(detail));
    }

    private boolean hasViolationKind(List<List<String>> rows, String kind) {
        return rows.stream().anyMatch(row -> row.get(0).equals(kind));
    }

    @Test
    void defaultWhitelistAllowsReadOnlyQuery() {
        // No config override: the hardcoded default whitelist applies. abs/count are in it.
        List<List<String>> rows = run("VALIDATE SELECT abs(v2), count(*) FROM t0 GROUP BY v2");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void plainColumnQueryIsValid() {
        List<List<String>> rows = run("VALIDATE SELECT v1, v2 FROM t0 WHERE v3 > 0");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void nonWhitelistedFunctionIsReported() {
        // hex is a real builtin but not in the default whitelist, and no config adds it.
        List<List<String>> rows = run("VALIDATE SELECT hex(v1) FROM t0");
        assertTrue(hasViolation(rows, "function", "hex"), rows.toString());
    }

    @Test
    void configIsAdditiveToDefault() {
        // Config adds hex; the default baseline (upper/cast) is still allowed, so the query is valid.
        Config.validate_allowed_functions = new String[] {"hex"};
        List<List<String>> rows = run("VALIDATE SELECT hex(v1), upper(cast(v1 AS VARCHAR)) FROM t0");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void functionAllowedByConfigIsValid() {
        Config.validate_allowed_functions = new String[] {"hex"};
        List<List<String>> rows = run("VALIDATE SELECT hex(v1) FROM t0");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void geospatialFunctionIsAllowedByDefault() {
        // st_* functions are in the default whitelist (synced with celostar), available from the start.
        List<List<String>> rows = run("VALIDATE SELECT st_distance_sphere(v1, v2, v3, v1) FROM t0");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void tableValuedFunctionNameIsRejected() {
        // Table functions are checked against the whitelist on the raw AST, before analysis resolves them (some
        // resolutions, e.g. files() with list_files_only, have side effects) -- so a disallowed one is a hard
        // analysis failure rather than a soft "function" violation row.
        assertThrows(SemanticException.class,
                () -> run("VALIDATE SELECT * FROM TABLE(generate_series(1, 10))"));
    }

    @Test
    void filesTableFunctionIsRejectedBeforeAnalysis() {
        // files() is never whitelisted. Rejecting it on the raw AST means analysis never runs and never touches
        // the (nonexistent) path -- if this instead threw from path resolution, that would mean the side effect
        // happened before the whitelist got a say.
        assertThrows(SemanticException.class,
                () -> run("VALIDATE SELECT * FROM FILES(\"format\" = \"parquet\", \"path\" = \"s3://bucket/x\")"));
    }

    @Test
    void filesNestedInSelectListScalarSubqueryIsRejected() {
        // AstTraverser.visitSelect traverses getOutputExpression(), which is null pre-analysis, so a FILES() nested
        // inside a scalar subquery in the SELECT list would otherwise never be reached by the raw-AST pass.
        assertThrows(SemanticException.class, () -> run(
                "VALIDATE SELECT (SELECT count(*) FROM FILES(\"format\" = \"parquet\", \"path\" = \"s3://bucket/x\"))"
                        + " FROM t0"));
    }

    @Test
    void selectStarNestedInSelectListScalarSubqueryIsRejected() {
        // Same traversal gap as above, but for a nested SELECT * instead of a nested table function.
        assertThrows(SemanticException.class, () -> run("VALIDATE SELECT (SELECT * FROM t0) FROM t1"));
    }

    @Test
    void allowedTableFunctionIsValid() {
        // unnest is not in the default whitelist; once added via config, a query using it validates clean.
        Config.validate_allowed_functions = new String[] {"unnest"};
        List<List<String>> rows = run("VALIDATE SELECT x FROM TABLE(unnest(ARRAY<INT>[1, 2, 3])) t(x)");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void nestedDisallowedFunctionInTableFunctionArgumentIsChecked() {
        // unnest itself is allowed via config, but hex nested inside its array argument is not -- the argument
        // traversal added for table functions must still catch it.
        Config.validate_allowed_functions = new String[] {"unnest"};
        List<List<String>> rows = run("VALIDATE SELECT x FROM TABLE(unnest([hex(1), 2])) t(x)");
        assertTrue(hasViolation(rows, "function", "hex"), rows.toString());
    }

    @Test
    void nestedDisallowedFunctionInRegularFunctionArgumentIsChecked() {
        // upper is in the default whitelist but hex, nested as its argument, is not; both the outer and the
        // nested call must be visited.
        List<List<String>> rows = run("VALIDATE SELECT upper(hex(v1)) FROM t0");
        assertTrue(hasViolation(rows, "function", "hex"), rows.toString());
    }

    @Test
    void valuesRowFunctionIsChecked() {
        // AstTraverser treats VALUES as a leaf by default; ValidateWhitelistChecker must recurse into rows itself.
        List<List<String>> rows = run("VALIDATE SELECT a, b FROM (VALUES (hex(1), 2)) t(a, b)");
        assertTrue(hasViolation(rows, "function", "hex"), rows.toString());
    }

    @Test
    void pivotAggregateFunctionIsChecked() {
        // AstTraverser treats PIVOT as a leaf by default; ValidateWhitelistChecker must recurse into it itself.
        // count_if is a real aggregate function but not in the default whitelist.
        List<List<String>> rows =
                run("VALIDATE SELECT v3 FROM t0 PIVOT (count_if(v1 > 0) FOR v2 IN (1, 2, 3))");
        assertTrue(hasViolation(rows, "function", "count_if"), rows.toString());
    }

    @Test
    void groupingFunctionsAreValid() {
        // GroupingFunctionCallExpr dispatches to visitGroupingFunctionCall, not visitFunctionCall; grouping/
        // grouping_id are in the default whitelist so a legitimate GROUPING SETS query still validates clean.
        List<List<String>> rows = run("VALIDATE SELECT v1, v2, GROUPING(v1), GROUPING_ID(v1, v2) FROM t0 " +
                "GROUP BY GROUPING SETS ((v1), (v1, v2))");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void lambdaProducesAstNodeViolation() {
        // array_map uses a lambda: LambdaFunctionExpr is not a whitelisted node, and array_map is not a default fn.
        List<List<String>> rows = run("VALIDATE SELECT array_map(x -> x + 1, [1, 2, 3])");
        assertTrue(hasViolationKind(rows, "ast_node"), rows.toString());
        assertTrue(hasViolation(rows, "function", "array_map"), rows.toString());
    }

    @Test
    void unionQueryIsValid() {
        // UNION produces a UnionRelation (a whitelisted node); its branches use only columns, so it validates clean.
        List<List<String>> rows = run("VALIDATE SELECT v1 FROM t0 UNION SELECT v4 FROM t1");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void unionBranchFunctionIsStillChecked() {
        // A non-whitelisted function in one UNION branch is still reported.
        List<List<String>> rows = run("VALIDATE SELECT v1 FROM t0 UNION ALL SELECT hex(v4) FROM t1");
        assertTrue(hasViolation(rows, "function", "hex"), rows.toString());
    }

    @Test
    void externalColumnViaExtensionsIsValid() {
        // A column defined elsewhere via EXTENSIONS resolves and produces no violations.
        List<List<String>> rows = run("VALIDATE SELECT virt FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void untypedExternalColumnViaExtensionsIsValid() {
        List<List<String>> rows = run("VALIDATE SELECT virt FROM t0 EXTENSIONS (test.t0.virt)");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void physicalColumnRestatementIsValid() {
        List<List<String>> rows = run("VALIDATE SELECT v1 FROM t0 EXTENSIONS (test.t0.v1)");
        assertTrue(isValid(rows), rows.toString());
    }

    @Test
    void extensionsDoNotLeakAfterValidate() {
        run("VALIDATE SELECT v1 FROM t0 EXTENSIONS (test.t0.virt : BIGINT)");
        assertNull(connectContext.getCelostarExtensions(),
                "celostarExtensions must be null on the test context after VALIDATE");
    }
}
