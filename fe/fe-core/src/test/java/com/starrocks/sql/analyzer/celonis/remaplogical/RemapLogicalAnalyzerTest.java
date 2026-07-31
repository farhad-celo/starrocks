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

package com.starrocks.sql.analyzer.celonis.remaplogical;

import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.Authorizer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.remaplogical.RemapLogicalStmt;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Tests for RemapLogicalAnalyzer: query-scope hint rejection and authorization of logical/mapped tables that do not
 * exist in the physical catalog (mirrors ValidateAnalyzerTest / ExplainInputColumnsAnalyzerTest).
 *
 * <p>Database is "test"; tables t0(v1,v2,v3 bigint).
 */
class RemapLogicalAnalyzerTest extends PlanTestBase {
    private static final String FILES_TABLE_FUNCTION = "FILES('path' = 's3://bucket/file.parquet', " +
            "'format' = 'parquet')";

    private static RemapLogicalStmt parse(String sql) {
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        return (RemapLogicalStmt) parsedStatement;
    }

    private static RemapLogicalStmt analyzeOk(String sql) {
        RemapLogicalStmt statement = parse(sql);
        assertDoesNotThrow(() -> Analyzer.analyze(statement, connectContext));
        return statement;
    }

    @Test
    void queryScopeHintThrows() {
        // AstBuilder attaches query-scope hints to the outermost statement (RemapLogicalStmt here), not to
        // getQueryStmt() — REMAP LOGICAL must reject them outright, matching VALIDATE.
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT /*+ SET_VAR(query_timeout=1) */ v1 FROM t0 " +
                        "LIMIT 500 MAPPINGS (TABLE test.t0 TO physical_t0)"), connectContext));
        assertTrue(exception.getMessage().contains("Query-scope hints are not supported in REMAP LOGICAL"),
                exception.getMessage());
    }

    @Test
    void authorizationSucceedsForLogicalTableNotInCatalog() {
        // "logical_table" has no backing table in the catalog; only the REMAP LOGICAL table mapping makes it
        // resolvable. Authorization runs after analysis has already restored the un-extended catalog on the
        // context, so it must re-derive the same schema extension itself to recognize this table.
        RemapLogicalStmt statement = analyzeOk("REMAP LOGICAL SELECT COUNT(*) FROM logical_table " +
                "LIMIT 500 MAPPINGS (TABLE test.logical_table TO physical_table)");
        assertDoesNotThrow(() -> Authorizer.check(statement, connectContext));
    }

    @Test
    void authorizationSucceedsForLogicalColumnNotInCatalog() {
        RemapLogicalStmt statement = analyzeOk("REMAP LOGICAL SELECT logical_v1 FROM t0 " +
                "LIMIT 500 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0, COLUMN test.t0.logical_v1 TO physical_v1) " +
                "EXTENSIONS (test.t0.logical_v1 : BIGINT)");
        assertDoesNotThrow(() -> Authorizer.check(statement, connectContext));
    }

    @Test
    void structExtensionsAreRejected() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT logical_struct FROM t0 " +
                        "LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                        "COLUMN test.t0.logical_struct TO physical_struct) " +
                        "EXTENSIONS (test.t0.logical_struct : STRUCT<field BIGINT>)"), connectContext));
        assertTrue(exception.getMessage().contains("does not support STRUCT extension columns: logical_struct"),
                exception.getMessage());
    }

    @Test
    void filesTableFunctionThrowsBeforeAnalysis() {
        // FILES(...) resolution during Analyzer.analyze lists remote paths and infers a schema -- not
        // side-effect-free -- and with list_files_only rewrites the relation into a ValuesRelation before
        // Authorizer.check() ever runs. Must be rejected on the raw AST, before any of that can happen.
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT * FROM " + FILES_TABLE_FUNCTION + " " +
                        "LIMIT 500 MAPPINGS (TABLE test.t0 TO physical_t0)"), connectContext));
        assertTrue(exception.getMessage().contains("does not support table function: files"),
                exception.getMessage());
    }

    @Test
    void unnestTableFunctionThrowsBeforeAnalysis() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT * FROM unnest([1,2,3]) " +
                        "LIMIT 500 MAPPINGS (TABLE test.t0 TO physical_t0)"), connectContext));
        assertTrue(exception.getMessage().contains("does not support table function: unnest"),
                exception.getMessage());
    }

    @Test
    void nestedFilesInSelectListThrowsBeforeAnalysis() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT (SELECT count(*) FROM " +
                        FILES_TABLE_FUNCTION + ") FROM t0 LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0)"), connectContext));
        assertTrue(exception.getMessage().contains("does not support table function: files"),
                exception.getMessage());
    }

    @Test
    void nestedFilesInGroupByThrowsBeforeAnalysis() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT 1 FROM t0 GROUP BY " +
                        "(SELECT count(*) FROM " + FILES_TABLE_FUNCTION + ") LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0)"), connectContext));
        assertTrue(exception.getMessage().contains("does not support table function: files"),
                exception.getMessage());
    }

    @Test
    void duplicateTableMappingThrows() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0, TABLE test.t0 TO other_physical_t0)"),
                        connectContext));
        assertTrue(exception.getMessage().contains("Duplicate REMAP LOGICAL table mapping"), exception.getMessage());
    }

    @Test
    void duplicateColumnMappingThrows() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 500 " +
                        "MAPPINGS (TABLE test.t0 TO physical_t0, " +
                        "COLUMN test.t0.v1 TO physical_v1, COLUMN test.t0.v1 TO other_physical_v1)"),
                        connectContext));
        assertTrue(exception.getMessage().contains("Duplicate REMAP LOGICAL column mapping"), exception.getMessage());
    }

    @Test
    void missingColumnMappingOnLogicalTableFails() {
        // logical_table has no backing catalog table, so the second EXTENSIONS column (logical_v2) needs its own
        // column mapping just like logical_v1 does -- omitting it must be rejected even though the table itself
        // resolves via its table mapping.
        SemanticException exception = assertThrows(SemanticException.class,
                () -> Analyzer.analyze(parse("REMAP LOGICAL SELECT logical_v1, logical_v2 FROM logical_table " +
                        "LIMIT 500 " +
                        "MAPPINGS (TABLE test.logical_table TO physical_table, " +
                        "COLUMN test.logical_table.logical_v1 TO physical_v1) " +
                        "EXTENSIONS (test.logical_table.logical_v1 : BIGINT, " +
                        "test.logical_table.logical_v2 : BIGINT)"), connectContext));
        assertTrue(exception.getMessage().contains(
                "Logical column 'test.logical_table.logical_v2' is not in the catalog and has no " +
                        "REMAP LOGICAL column mapping"), exception.getMessage());
    }

    @Test
    void starExpansionResolvesCaseDifferentTableSpelling() {
        // SELECT * forces expandStarForEntry to look up the query table in the collected table map; the raw
        // qualifier here ("T0") differs in case from the catalog table ("t0"), so the exact-key lookup misses and
        // it must fall back to the case-insensitive/normalized match to expand the star into real column names.
        RemapLogicalStmt statement = analyzeOk("REMAP LOGICAL SELECT * FROM T0 LIMIT 500 " +
                "MAPPINGS (TABLE test.T0 TO wrapped_t0)");
        assertDoesNotThrow(() -> Authorizer.check(statement, connectContext));
    }
}
