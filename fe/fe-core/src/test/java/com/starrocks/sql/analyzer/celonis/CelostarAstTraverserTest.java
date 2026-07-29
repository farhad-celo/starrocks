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

import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class CelostarAstTraverserTest extends PlanTestBase {
    @Test
    void visitsAnalyticFunctionWrapperOnce() {
        assertEquals(List.of("row_number"),
                collectFunctions("SELECT row_number() OVER () FROM t0"));
    }

    @Test
    void visitsSetOperationOrderByExpressions() {
        assertEquals(List.of("hex"),
                collectFunctions("SELECT v1 AS x FROM t0 " +
                        "UNION ALL SELECT v4 FROM t1 ORDER BY hex(x) LIMIT 100"));
    }

    private List<String> collectFunctions(String sql) {
        StatementBase statement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        Analyzer.analyze(statement, connectContext);

        List<String> functions = new ArrayList<>();
        new CelostarAstTraverser() {
            @Override
            public Void visitFunctionCall(FunctionCallExpr node, Void context) {
                functions.add(node.getFnName().getFunction());
                return super.visitFunctionCall(node, context);
            }
        }.visit(statement);
        return functions;
    }
}
