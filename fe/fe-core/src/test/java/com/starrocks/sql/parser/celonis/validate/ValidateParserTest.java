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

package com.starrocks.sql.parser.celonis.validate;

import com.google.common.collect.ImmutableList;
import com.starrocks.catalog.PrimitiveType;
import com.starrocks.qe.SessionVariable;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.celonis.validate.ValidateStmt;
import com.starrocks.sql.parser.SqlParser;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ValidateParserTest {

    private ValidateStmt parse(String sql) {
        List<StatementBase> statements = SqlParser.parse(sql, new SessionVariable());
        assertEquals(1, statements.size());
        return assertInstanceOf(ValidateStmt.class, statements.get(0));
    }

    @Test
    void parseWithoutExtensions() {
        ValidateStmt statement = parse("VALIDATE SELECT v1 FROM t0");
        assertTrue(statement.getVirtualExtensions().isEmpty());
    }

    @Test
    void parseWithExtensions() {
        ValidateStmt statement = parse("VALIDATE SELECT a FROM db1.t1 EXTENSIONS "
                + "(db1.t1.col1 : BIGINT, cat2.db2.t2.x : VARCHAR(20))");
        assertEquals(2, statement.getVirtualExtensions().size());
        assertEquals(ImmutableList.of("db1", "t1"), statement.getVirtualExtensions().get(0).tablePath());
        assertEquals("col1", statement.getVirtualExtensions().get(0).column());
        assertEquals(PrimitiveType.BIGINT, statement.getVirtualExtensions().get(0).type().getPrimitiveType());
        assertEquals(ImmutableList.of("cat2", "db2", "t2"), statement.getVirtualExtensions().get(1).tablePath());
        assertEquals("x", statement.getVirtualExtensions().get(1).column());
    }

    @Test
    void parseEmptyExtensionList() {
        ValidateStmt statement = parse("VALIDATE SELECT 1 EXTENSIONS ()");
        assertTrue(statement.getVirtualExtensions().isEmpty());
    }

    @Test
    void validateIsNotReserved() {
        // VALIDATE is a non-reserved keyword, so it is still usable as an identifier (e.g. column alias).
        List<StatementBase> statements = SqlParser.parse("SELECT 1 AS validate", new SessionVariable());
        assertEquals(1, statements.size());
    }
}
