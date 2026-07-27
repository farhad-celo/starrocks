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

import com.starrocks.analysis.Expr;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.PivotAggregation;
import com.starrocks.sql.ast.PivotRelation;
import com.starrocks.sql.ast.PivotValue;
import com.starrocks.sql.ast.ValuesRelation;

import java.util.List;

/**
 * The stock {@link AstTraverser} treats VALUES and PIVOT as leaves, so it never descends into their row/aggregate/
 * pivot-value expressions. Both raw-AST and analyzed-AST VALIDATE passes need to see into those expressions, so this
 * shared base supplies the fix-up once instead of each pass reimplementing it identically.
 */
abstract class AbstractRelationLeafFixupVisitor extends AstTraverser<Void, Void> {
    @Override
    public Void visitValues(ValuesRelation node, Void context) {
        for (List<Expr> row : node.getRows()) {
            for (Expr expr : row) {
                visit(expr, context);
            }
        }
        return null;
    }

    @Override
    public Void visitPivotRelation(PivotRelation node, Void context) {
        visit(node.getQuery(), context);
        for (PivotAggregation aggregation : node.getAggregateFunctions()) {
            visit(aggregation.getFunctionCallExpr(), context);
        }
        for (PivotValue pivotValue : node.getPivotValues()) {
            for (Expr expr : pivotValue.getExprs()) {
                visit(expr, context);
            }
        }
        return null;
    }
}
