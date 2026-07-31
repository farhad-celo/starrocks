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

package com.starrocks.sql.analyzer.celonis.explaininputcolumns;

import com.starrocks.analysis.ParseNode;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.analyzer.celonis.CelostarExtensionScope;
import com.starrocks.sql.analyzer.celonis.CelostarSchemaExtensionResolver;
import com.starrocks.sql.analyzer.celonis.LogicalSchemaExtensionAnalyzer;
import com.starrocks.sql.analyzer.celonis.RawAstSelectTraverser;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;

public final class ExplainInputColumnsAnalyzer {
    private ExplainInputColumnsAnalyzer() {
    }

    public static void analyze(ExplainInputColumnsStmt statement, ConnectContext connectContext) {
        LogicalSchemaExtensionAnalyzer.validateInnerQuery(statement.getQueryStmt(), "EXPLAIN INPUT COLUMNS");
        rejectSelectStar(statement.getQueryStmt());
        CelostarSchemaExtension schemaExtension =
                CelostarSchemaExtensionResolver.resolveValidated(statement.getVirtualExtensions(), connectContext);
        // Authorization reuses this rather than resolving again, so the two phases cannot disagree about which
        // references are virtual.
        statement.setResolvedExtensions(schemaExtension);
        // Install extensions only while the inner query is analyzed so normal statements keep the unextended catalog.
        try (CelostarExtensionScope ignored = CelostarExtensionScope.install(connectContext, schemaExtension)) {
            Analyzer.analyze(statement.getQueryStmt(), connectContext);
        }
    }

    private static void rejectSelectStar(QueryStatement queryStmt) {
        new SelectStarRejectionVisitor().visit(queryStmt);
    }

    /**
     * Walks the raw select-list items and GROUP BY expressions (via {@link RawAstSelectTraverser}), not just the
     * top-level select list, so a {@code SELECT *} nested in a select-list or GROUP BY subquery is also rejected.
     */
    private static final class SelectStarRejectionVisitor extends RawAstSelectTraverser {
        @Override
        public Void visit(ParseNode node, Void context) {
            return node == null ? null : node.accept(this, context);
        }

        @Override
        protected void visitSelectListItem(SelectListItem item, Void context) {
            if (item.isStar()) {
                throw new SemanticException("SELECT * is not supported in EXPLAIN INPUT COLUMNS");
            }
            super.visitSelectListItem(item, context);
        }
    }
}
