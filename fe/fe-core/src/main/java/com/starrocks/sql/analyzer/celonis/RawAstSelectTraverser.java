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

import com.starrocks.analysis.GroupByClause;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.SelectRelation;

/**
 * Traverses a parsed {@link SelectRelation} before analysis has populated its output expressions.
 *
 * <p>{@code AstTraverser#visitSelect} only walks {@code getOutputExpression()}, which is empty on the raw AST.
 * Commands that need to validate a query before analysis can extend this visitor to traverse the parsed SELECT list
 * and GROUP BY payloads in addition to the relation and clause traversal supplied by {@link CelostarAstTraverser}.
 */
public abstract class RawAstSelectTraverser extends CelostarAstTraverser {
    @Override
    public Void visitSelect(SelectRelation node, Void context) {
        visitSelectList(node, context);
        visitGroupBy(node, context);
        return super.visitSelect(node, context);
    }

    private void visitSelectList(SelectRelation node, Void context) {
        if (node.getSelectList() == null) {
            return;
        }
        for (SelectListItem item : node.getSelectList().getItems()) {
            visitSelectListItem(item, context);
        }
    }

    protected void visitSelectListItem(SelectListItem item, Void context) {
        if (!item.isStar()) {
            visit(item.getExpr(), context);
        }
    }

    private void visitGroupBy(SelectRelation node, Void context) {
        GroupByClause groupByClause = node.getGroupByClause();
        if (groupByClause == null) {
            return;
        }
        if (groupByClause.getOriGroupingExprs() != null) {
            groupByClause.getOriGroupingExprs().forEach(expr -> visit(expr, context));
        }
        if (groupByClause.getGroupingSetList() != null) {
            groupByClause.getGroupingSetList().forEach(
                    groupingSet -> groupingSet.forEach(expr -> visit(expr, context)));
        }
    }
}
