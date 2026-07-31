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

package com.starrocks.sql.parser.celonis.explaininputcolumns;

import com.google.common.collect.ImmutableList;
import com.starrocks.catalog.PrimitiveType;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.qe.SessionVariable;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;
import com.starrocks.sql.ast.celonis.validate.ValidateStmt;
import com.starrocks.sql.parser.ParsingException;
import com.starrocks.sql.parser.SqlParser;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ExplainInputColumnsParserTest {

    @Test
    void parseWithoutExtensions() {
        String sql = "EXPLAIN INPUT COLUMNS SELECT 1";
        List<StatementBase> statements = SqlParser.parse(sql, new SessionVariable());
        assertEquals(1, statements.size());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertTrue(statement.getVirtualExtensions().isEmpty());
    }

    @Test
    void parseWithExtensions() {
        String sql = "EXPLAIN INPUT COLUMNS SELECT a FROM db1.t1 EXTENSIONS " +
                "(db1.t1.col1 : BIGINT, cat2.db2.t2.x : VARCHAR(20))";
        List<StatementBase> statements = SqlParser.parse(sql, new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertEquals(2, statement.getVirtualExtensions().size());
        CelostarSchemaExtensionSpec firstExtension = statement.getVirtualExtensions().get(0);
        assertEquals(ImmutableList.of("db1", "t1"), firstExtension.tablePath());
        assertEquals("col1", firstExtension.column());
        assertEquals(PrimitiveType.BIGINT, firstExtension.type().getPrimitiveType());
        CelostarSchemaExtensionSpec secondExtension = statement.getVirtualExtensions().get(1);
        assertEquals(ImmutableList.of("cat2", "db2", "t2"), secondExtension.tablePath());
        assertEquals("x", secondExtension.column());
        ScalarType secondExtensionType = (ScalarType) secondExtension.type();
        assertEquals(PrimitiveType.VARCHAR, secondExtensionType.getPrimitiveType());
        assertEquals(20, secondExtensionType.getLength());
    }

    @Test
    void parseEmptyExtensionList() {
        // EXTENSIONS () with zero items must be accepted.
        List<StatementBase> statements =
                SqlParser.parse("EXPLAIN INPUT COLUMNS SELECT 1 EXTENSIONS ()", new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertTrue(statement.getVirtualExtensions().isEmpty());
    }

    @Test
    void parseOnePartExtension() {
        // Single-part table paths are valid at parser level.
        List<StatementBase> statements =
                SqlParser.parse("EXPLAIN INPUT COLUMNS SELECT 1 EXTENSIONS (t.col : BIGINT)", new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertEquals(1, statement.getVirtualExtensions().size());
        assertEquals(ImmutableList.of("t"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("col", statement.getVirtualExtensions().get(0).column());
        assertEquals(PrimitiveType.BIGINT, statement.getVirtualExtensions().get(0).type().getPrimitiveType());
    }

    @Test
    void parseThreePartExtension() {
        // catalog.db.table.col is parsed as a three-part table path plus column name.
        List<StatementBase> statements = SqlParser.parse(
                "EXPLAIN INPUT COLUMNS SELECT 1 EXTENSIONS (cat.db.t.col : BIGINT)", new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertEquals(ImmutableList.of("cat", "db", "t"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("col", statement.getVirtualExtensions().get(0).column());
        assertEquals(PrimitiveType.BIGINT, statement.getVirtualExtensions().get(0).type().getPrimitiveType());
    }

    @Test
    void parseVirtualTableExtension() {
        List<StatementBase> statements = SqlParser.parse(
                "EXPLAIN INPUT COLUMNS SELECT virt FROM vtab EXTENSIONS (test.vtab.virt : BIGINT)",
                new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertEquals(ImmutableList.of("test", "vtab"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("virt", statement.getVirtualExtensions().get(0).column());
        assertEquals(PrimitiveType.BIGINT, statement.getVirtualExtensions().get(0).type().getPrimitiveType());
    }

    @Test
    void parseExtensionWithoutType() {
        List<StatementBase> statements =
                SqlParser.parse("EXPLAIN INPUT COLUMNS SELECT 1 EXTENSIONS (t.col)", new SessionVariable());
        ExplainInputColumnsStmt statement = assertAndCast(statements.get(0));
        assertEquals(1, statement.getVirtualExtensions().size());
        assertEquals(ImmutableList.of("t"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("col", statement.getVirtualExtensions().get(0).column());
        assertNull(statement.getVirtualExtensions().get(0).type());
    }

    @Test
    void parseMixedTypedAndUntypedExtensions() {
        List<StatementBase> statements = SqlParser.parse(
                "VALIDATE SELECT 1 EXTENSIONS (t.untyped, t.typed : BIGINT)", new SessionVariable());
        List<CelostarSchemaExtensionSpec> extensions =
                ((ValidateStmt) statements.get(0)).getVirtualExtensions();
        assertEquals(2, extensions.size());
        assertNull(extensions.get(0).type());
        assertEquals(Type.BIGINT, extensions.get(1).type());
    }

    @Test
    void rejectInnerExplain() {
        assertThrows(ParsingException.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS EXPLAIN SELECT 1", new SessionVariable()));
    }

    @Test
    void rejectInsertInner() {
        // queryStatement does not accept INSERT.
        assertThrows(Exception.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS INSERT INTO t VALUES (1)", new SessionVariable()));
    }

    @Test
    void rejectWriteInner() {
        assertThrows(Exception.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS UPDATE t SET c = 1", new SessionVariable()));
        assertThrows(Exception.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS DELETE FROM t WHERE c = 1", new SessionVariable()));
    }

    @Test
    void rejectDdlInner() {
        assertThrows(Exception.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS CREATE TABLE t (c INT)", new SessionVariable()));
        assertThrows(Exception.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS DROP TABLE t", new SessionVariable()));
    }

    @Test
    void rejectOutfileInner() {
        assertThrows(ParsingException.class,
                () -> SqlParser.parse("EXPLAIN INPUT COLUMNS SELECT 1 INTO OUTFILE \"file:///tmp/out\"",
                        new SessionVariable()));
    }

    private static ExplainInputColumnsStmt assertAndCast(StatementBase statement) {
        return assertInstanceOf(ExplainInputColumnsStmt.class, statement);
    }
}
