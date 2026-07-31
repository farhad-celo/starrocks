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

package com.starrocks.sql.parser.celonis.remaplogical;

import com.google.common.collect.ImmutableList;
import com.starrocks.catalog.PrimitiveType;
import com.starrocks.qe.SessionVariable;
import com.starrocks.sql.ast.AstVisitor;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.remaplogical.RemapLogicalStmt;
import com.starrocks.sql.parser.ParsingException;
import com.starrocks.sql.parser.SqlParser;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RemapLogicalParserTest {

    @Test
    void parseMappingsAndExtensions() {
        String sql = "REMAP LOGICAL SELECT logical_col FROM db1.t1 MAPPINGS " +
                "(TABLE db1.t1 TO t2, COLUMN db1.t1.logical_col TO physical_col) " +
                "EXTENSIONS (db1.t1.logical_col : BIGINT)";
        RemapLogicalStmt statement = parseOne(sql);

        assertEquals(1, statement.getTableMappings().size());
        assertEquals(ImmutableList.of("db1", "t1"), statement.getTableMappings().get(0).tablePath());
        assertEquals("t2", statement.getTableMappings().get(0).targetTable());

        assertEquals(1, statement.getColumnMappings().size());
        assertEquals(ImmutableList.of("db1", "t1"), statement.getColumnMappings().get(0).tablePath());
        assertEquals("logical_col", statement.getColumnMappings().get(0).column());
        assertEquals("physical_col", statement.getColumnMappings().get(0).targetColumn());

        assertEquals(1, statement.getVirtualExtensions().size());
        assertEquals(ImmutableList.of("db1", "t1"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("logical_col", statement.getVirtualExtensions().get(0).column());
        assertEquals(PrimitiveType.BIGINT, statement.getVirtualExtensions().get(0).type().getPrimitiveType());
    }

    @Test
    void parseExtensionWithoutType() {
        // REMAP LOGICAL shares the celostarExtension production, so the type is optional here as well.
        RemapLogicalStmt statement = parseOne("REMAP LOGICAL SELECT logical_col FROM db1.t1 MAPPINGS " +
                "(COLUMN db1.t1.logical_col TO physical_col) EXTENSIONS (db1.t1.logical_col)");

        assertEquals(1, statement.getVirtualExtensions().size());
        assertEquals("logical_col", statement.getVirtualExtensions().get(0).column());
        assertNull(statement.getVirtualExtensions().get(0).type());
    }

    @Test
    void parseEmptyMappingList() {
        RemapLogicalStmt statement = parseOne("REMAP LOGICAL SELECT 1 MAPPINGS ()");
        assertTrue(statement.getTableMappings().isEmpty());
        assertTrue(statement.getColumnMappings().isEmpty());
        assertTrue(statement.getVirtualExtensions().isEmpty());
    }

    @Test
    void acceptUsesDefaultAstVisitorFallback() {
        RemapLogicalStmt statement = parseOne("REMAP LOGICAL SELECT 1 MAPPINGS ()");
        AstVisitor<StatementBase, Void> visitor = new AstVisitor<>() {
            @Override
            public StatementBase visitStatement(StatementBase visitedStatement, Void context) {
                return visitedStatement;
            }
        };

        assertSame(statement, statement.accept(visitor, null));
    }

    @Test
    void rejectInnerExplain() {
        assertThrows(ParsingException.class,
                () -> SqlParser.parse("REMAP LOGICAL EXPLAIN SELECT 1 MAPPINGS ()", new SessionVariable()));
    }

    @Test
    void rejectOutfileInner() {
        assertThrows(ParsingException.class,
                () -> SqlParser.parse("REMAP LOGICAL SELECT 1 INTO OUTFILE \"file:///tmp/out\" MAPPINGS ()",
                        new SessionVariable()));
    }

    private static RemapLogicalStmt parseOne(String sql) {
        List<StatementBase> statements = SqlParser.parse(sql, new SessionVariable());
        assertEquals(1, statements.size());
        return assertInstanceOf(RemapLogicalStmt.class, statements.get(0));
    }
}
