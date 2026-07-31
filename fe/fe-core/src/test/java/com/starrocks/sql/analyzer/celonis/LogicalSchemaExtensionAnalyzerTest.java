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

package com.starrocks.sql.analyzer.celonis;

import com.starrocks.analysis.OutFileClause;
import com.starrocks.analysis.TableName;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Unit tests for LogicalSchemaExtensionAnalyzer, shared by REMAP LOGICAL and EXPLAIN INPUT COLUMNS. The grammar
 * already rejects EXPLAIN/OUTFILE and malformed table paths at parse time for both statements, so
 * validateInnerQuery/pathToTableName's own checks are exercised directly here rather than through a full SQL parse.
 */
class LogicalSchemaExtensionAnalyzerTest extends PlanTestBase {

    private static QueryStatement parseQuery(String sql) {
        StatementBase statement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        return (QueryStatement) statement;
    }

    @Test
    void validateInnerQueryAcceptsPlainSelect() {
        assertDoesNotThrow(() -> LogicalSchemaExtensionAnalyzer.validateInnerQuery(
                parseQuery("SELECT v1 FROM t0"), "TEST STATEMENT"));
    }

    @Test
    void validateInnerQueryRejectsExplain() {
        QueryStatement queryStmt = parseQuery("SELECT v1 FROM t0");
        queryStmt.setIsExplain(true, null);
        SemanticException exception = assertThrows(SemanticException.class,
                () -> LogicalSchemaExtensionAnalyzer.validateInnerQuery(queryStmt, "TEST STATEMENT"));
        assertTrue(exception.getMessage().contains("Inner query of TEST STATEMENT must not be an EXPLAIN"),
                exception.getMessage());
    }

    @Test
    void validateInnerQueryRejectsOutFileClause() {
        QueryStatement queryStmt = parseQuery("SELECT v1 FROM t0");
        queryStmt.setOutFileClause(new OutFileClause("file:///tmp/out", null, null));
        SemanticException exception = assertThrows(SemanticException.class,
                () -> LogicalSchemaExtensionAnalyzer.validateInnerQuery(queryStmt, "TEST STATEMENT"));
        assertTrue(exception.getMessage().contains("INTO OUTFILE is not supported in TEST STATEMENT"),
                exception.getMessage());
    }

    @Test
    void pathToTableNameResolvesOnePart() {
        TableName tableName = LogicalSchemaExtensionAnalyzer.pathToTableName(List.of("t0"), "path");
        assertNull(tableName.getCatalog());
        assertNull(tableName.getDb());
        assertEquals("t0", tableName.getTbl());
    }

    @Test
    void pathToTableNameResolvesTwoParts() {
        TableName tableName = LogicalSchemaExtensionAnalyzer.pathToTableName(List.of("test", "t0"), "path");
        assertNull(tableName.getCatalog());
        assertEquals("test", tableName.getDb());
        assertEquals("t0", tableName.getTbl());
    }

    @Test
    void pathToTableNameResolvesThreeParts() {
        TableName tableName = LogicalSchemaExtensionAnalyzer.pathToTableName(
                List.of("default_catalog", "test", "t0"), "path");
        assertEquals("default_catalog", tableName.getCatalog());
        assertEquals("test", tableName.getDb());
        assertEquals("t0", tableName.getTbl());
    }

    @Test
    void pathToTableNameRejectsEmptyPath() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> LogicalSchemaExtensionAnalyzer.pathToTableName(List.of(), "path description"));
        assertTrue(exception.getMessage().contains("Invalid path description"), exception.getMessage());
    }

    @Test
    void pathToTableNameRejectsOversizedPath() {
        SemanticException exception = assertThrows(SemanticException.class,
                () -> LogicalSchemaExtensionAnalyzer.pathToTableName(
                        List.of("a", "b", "c", "d"), "path description"));
        assertTrue(exception.getMessage().contains("Invalid path description"), exception.getMessage());
    }
}
