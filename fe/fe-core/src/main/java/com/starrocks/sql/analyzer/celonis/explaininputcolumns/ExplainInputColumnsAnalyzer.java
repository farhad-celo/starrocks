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
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;

public final class ExplainInputColumnsAnalyzer {
    private ExplainInputColumnsAnalyzer() {
    }

    public static void analyze(ExplainInputColumnsStmt statement, ConnectContext connectContext) {
        LogicalSchemaExtensionAnalyzer.validateInnerQuery(statement.getQueryStmt(), "EXPLAIN INPUT COLUMNS");
        rejectSelectStar(statement.getQueryStmt());
        CelostarSchemaExtension schemaExtension =
                CelostarSchemaExtensionResolver.resolveValidated(statement.getVirtualExtensions(), connectContext);
        // Install extensions only while the inner query is analyzed so normal statements keep the unextended catalog.
        try (CelostarExtensionScope ignored = CelostarExtensionScope.install(connectContext, schemaExtension)) {
            Analyzer.analyze(statement.getQueryStmt(), connectContext);
        }
    }

    private static void rejectSelectStar(QueryStatement queryStmt) {
        new AstTraverser<Void, Void>() {
            @Override
            public Void visit(ParseNode node, Void context) {
                return node == null ? null : node.accept(this, context);
            }

            @Override
            public Void visitSelect(SelectRelation node, Void context) {
                if (node.getSelectList() != null &&
                        node.getSelectList().getItems().stream().anyMatch(SelectListItem::isStar)) {
                    throw new SemanticException("SELECT * is not supported in EXPLAIN INPUT COLUMNS");
                }
                return super.visitSelect(node, context);
            }
        }.visit(queryStmt);
    }
}
