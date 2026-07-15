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
import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Database;
import com.starrocks.catalog.Table;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

public final class ExplainInputColumnsAnalyzer {
    private ExplainInputColumnsAnalyzer() {
    }

    public static void analyze(ExplainInputColumnsStmt statement, ConnectContext connectContext) {
        validateInnerQuery(statement.getQueryStmt());
        CelostarSchemaExtension schemaExtension = resolveExtensions(statement, connectContext);
        CelostarExtensionSet previousCelostarExtensions = connectContext.getCelostarExtensions();
        // Install extensions only while the inner query is analyzed so normal statements keep the unextended catalog.
        connectContext.setCelostarExtensions(CelostarExtensionSet.ofSchemaExtension(schemaExtension));
        try {
            Analyzer.analyze(statement.getQueryStmt(), connectContext);
        } finally {
            connectContext.setCelostarExtensions(previousCelostarExtensions);
        }
    }

    private static void validateInnerQuery(QueryStatement queryStmt) {
        if (queryStmt.isExplain()) {
            throw new SemanticException("Inner query of EXPLAIN INPUT COLUMNS must not be an EXPLAIN");
        }
        if (queryStmt.hasOutFileClause()) {
            throw new SemanticException("INTO OUTFILE is not supported in EXPLAIN INPUT COLUMNS");
        }
        rejectSelectStar(queryStmt);
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

    private static CelostarSchemaExtension resolveExtensions(ExplainInputColumnsStmt statement, ConnectContext connectContext) {
        CelostarSchemaExtension schemaExtension = new CelostarSchemaExtension();
        Set<ExtensionColumnKey> seenExtensions = new HashSet<>();
        com.starrocks.server.MetadataMgr metadataManager = GlobalStateMgr.getCurrentState().getMetadataMgr();

        for (ExplainInputColumnsStmt.VirtualExtension virtualExtension : statement.getVirtualExtensions()) {
            TableName tableName = tableName(virtualExtension);
            tableName.normalization(connectContext);

            ExtensionTableKey tableKey = new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(),
                    tableName.getTbl());
            if (!seenExtensions.add(new ExtensionColumnKey(tableKey, virtualExtension.column()))) {
                throw new SemanticException("Duplicate EXPLAIN INPUT COLUMNS extension: %s.%s",
                        tableName.getTbl(), virtualExtension.column());
            }

            if (!GlobalStateMgr.getCurrentState().getCatalogMgr().catalogExists(tableName.getCatalog())) {
                throw new SemanticException("Extension refers to unknown catalog: %s", tableName.getCatalog());
            }
            Database database = metadataManager.getDb(connectContext, tableName.getCatalog(), tableName.getDb());
            if (database == null) {
                throw new SemanticException("Extension refers to unknown database: %s.%s", tableName.getCatalog(),
                        tableName.getDb());
            }

            Table table = metadataManager.getTable(connectContext, tableName.getCatalog(), tableName.getDb(),
                    tableName.getTbl());
            if (table != null && table.getColumn(virtualExtension.column()) != null) {
                throw new SemanticException(
                        "Extension column '%s' already exists on table '%s'", virtualExtension.column(),
                        tableName.getTbl());
            }

            schemaExtension.add(tableKey, virtualExtension.column(), virtualExtension.type());
        }
        return schemaExtension;
    }

    private static TableName tableName(ExplainInputColumnsStmt.VirtualExtension virtualExtension) {
        try {
            return virtualExtension.tableName();
        } catch (IllegalArgumentException e) {
            throw new SemanticException(e.getMessage());
        }
    }

    private record ExtensionColumnKey(ExtensionTableKey tableKey, String column) {
        private ExtensionColumnKey {
            column = column == null ? "" : column.toLowerCase(Locale.ROOT);
        }
    }
}
