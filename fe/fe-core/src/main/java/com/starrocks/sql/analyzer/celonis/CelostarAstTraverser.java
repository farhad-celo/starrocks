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

import com.starrocks.analysis.AnalyticExpr;
import com.starrocks.analysis.Expr;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.JoinRelation;
import com.starrocks.sql.ast.NormalizedTableFunctionRelation;
import com.starrocks.sql.ast.PivotAggregation;
import com.starrocks.sql.ast.PivotRelation;
import com.starrocks.sql.ast.PivotValue;
import com.starrocks.sql.ast.SetOperationRelation;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.ValuesRelation;

import java.util.List;

/**
 * Completes query traversal for payloads that the stock {@link AstTraverser} treats as relation leaves or stores
 * outside normal expression children.
 *
 * Command-specific visitors can override staged relation methods and reuse {@link #visitJoinHints} or
 * {@link #visitPivotExpressions} when expressions need a specialized analysis scope.
 *
 * Inline CTE definitions retain the stock {@link AstTraverser} behavior: WITH declarations are visited from
 * their containing SELECT or set operation, and each declaration's query is traversed through {@code visitCTE}.
 */
public abstract class CelostarAstTraverser extends AstTraverser<Void, Void> {
    @Override
    public Void visitSetOp(SetOperationRelation node, Void context) {
        super.visitSetOp(node, context);
        visitSetOperationOrderBy(node, context);
        return null;
    }

    protected void visitSetOperationOrderBy(SetOperationRelation node, Void context) {
        if (node.getOrderBy() != null) {
            node.getOrderBy().forEach(element -> visit(element.getExpr(), context));
        }
    }

    @Override
    public Void visitJoin(JoinRelation node, Void context) {
        super.visitJoin(node, context);
        visitJoinHints(node, context);
        return null;
    }

    protected final void visitJoinHints(JoinRelation node, Void context) {
        if (node.getSkewColumn() != null) {
            visit(node.getSkewColumn(), context);
        }
        if (node.getSkewValues() != null) {
            node.getSkewValues().forEach(value -> visit(value, context));
        }
    }

    @Override
    public Void visitValues(ValuesRelation node, Void context) {
        for (List<Expr> row : node.getRows()) {
            row.forEach(expression -> visit(expression, context));
        }
        return null;
    }

    @Override
    public Void visitTableFunction(TableFunctionRelation node, Void context) {
        List<Expr> arguments = node.getChildExpressions();
        if (arguments == null && node.getFunctionParams() != null) {
            arguments = node.getFunctionParams().exprs();
        }
        if (arguments != null) {
            arguments.forEach(argument -> visit(argument, context));
        }
        return null;
    }

    @Override
    public Void visitNormalizedTableFunction(NormalizedTableFunctionRelation node, Void context) {
        if (node.getLeft() != null) {
            visit(node.getLeft(), context);
        }
        if (node.getRight() != null) {
            visit(node.getRight(), context);
        }
        return null;
    }

    @Override
    public Void visitPivotRelation(PivotRelation node, Void context) {
        if (node.getQuery() != null) {
            visit(node.getQuery(), context);
        }
        visitPivotExpressions(node, context);
        return null;
    }

    protected final void visitPivotExpressions(PivotRelation node, Void context) {
        for (PivotAggregation aggregation : node.getAggregateFunctions()) {
            visit(aggregation.getFunctionCallExpr(), context);
        }
        node.getPivotColumns().forEach(column -> visit(column, context));
        for (PivotValue pivotValue : node.getPivotValues()) {
            pivotValue.getExprs().forEach(expression -> visit(expression, context));
        }
        // Analysis derives physical group-by inputs that are not present in the original PIVOT payload.
        node.getGroupByKeys().forEach(expression -> visit(expression, context));
    }

    @Override
    public Void visitAnalyticExpr(AnalyticExpr node, Void context) {
        int functionArgumentCount = node.getFnCall().getChildren().size();
        // AnalyticExpr deliberately stores only the function arguments as children. Visit the FunctionCallExpr
        // wrapper so consumers can validate its name, then visit the remaining analytic payload without visiting
        // those arguments twice.
        visit(node.getFnCall(), context);
        for (int i = functionArgumentCount; i < node.getChildren().size(); i++) {
            visit(node.getChild(i), context);
        }
        if (node.getSkewColumn() != null) {
            visit(node.getSkewColumn(), context);
        }
        if (node.getSkewValues() != null) {
            node.getSkewValues().forEach(value -> visit(value, context));
        }
        return null;
    }
}
