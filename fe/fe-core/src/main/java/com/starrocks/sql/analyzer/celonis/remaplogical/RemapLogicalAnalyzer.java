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

package com.starrocks.sql.analyzer.celonis.remaplogical;

import com.starrocks.analysis.FunctionName;
import com.starrocks.analysis.LimitElement;
import com.starrocks.analysis.ParseNode;
import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.Table;
import com.starrocks.common.Config;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.AnalyzerUtils;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.analyzer.celonis.CelostarExtensionScope;
import com.starrocks.sql.analyzer.celonis.CelostarSchemaExtensionResolver;
import com.starrocks.sql.analyzer.celonis.LogicalSchemaExtensionAnalyzer;
import com.starrocks.sql.analyzer.celonis.RawAstSelectTraverser;
import com.starrocks.sql.ast.FileTableFunctionRelation;
import com.starrocks.sql.ast.QueryRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;
import com.starrocks.sql.ast.celonis.remaplogical.RemapLogicalStmt;
import com.starrocks.sql.ast.celonis.remaplogical.RemappingSet;

import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

public final class RemapLogicalAnalyzer {
    private static final String STATEMENT_NAME = "REMAP LOGICAL";

    private RemapLogicalAnalyzer() {
    }

    public static void analyze(RemapLogicalStmt statement, ConnectContext connectContext) {
        if (statement.isExistQueryScopeHint()) {
            throw new SemanticException("Query-scope hints are not supported in " + STATEMENT_NAME);
        }
        LogicalSchemaExtensionAnalyzer.validateInnerQuery(statement.getQueryStmt(), STATEMENT_NAME);
        rejectTableFunctions(statement.getQueryStmt());
        rejectStructExtensions(statement);
        validateParsedTopLevelLimit(statement);
        RemappingSet remappingSet = resolveRemappings(statement, connectContext);
        statement.setRemappingSet(remappingSet);

        CelostarSchemaExtension schemaExtension = buildSchemaExtension(statement, connectContext);
        // Install extensions only while the inner query is analyzed so normal statements keep the unextended catalog.
        try (CelostarExtensionScope ignored = CelostarExtensionScope.install(connectContext, schemaExtension)) {
            Analyzer.analyze(statement.getQueryStmt(), connectContext);
        }

        validateTopLevelLimitValue(statement);
        validateUnmappedLogicalReferences(statement, connectContext);
    }

    /**
     * Builds the schema extension covering both the statement's declared virtual extensions and the
     * table-mapping source tables, so that logical/mapped table and column references resolve. Used both while
     * analyzing the inner query and, later, while authorizing it (see {@code AuthorizerStmtVisitor}).
     */
    public static CelostarSchemaExtension buildSchemaExtension(RemapLogicalStmt statement,
                                                                ConnectContext connectContext) {
        CelostarSchemaExtension schemaExtension = CelostarSchemaExtensionResolver.resolveValidated(
                statement.getVirtualExtensions(), connectContext);
        registerMappedVirtualTables(statement, schemaExtension, connectContext);
        return schemaExtension;
    }

    /**
     * Rejects any table function ({@code FILES(...)}, {@code unnest}, celonis TVFs, ...) on the raw, unanalyzed AST
     * before {@link Analyzer#analyze} runs. Resolving a table function during analysis is not side-effect-free --
     * {@code files()} lists remote paths and infers a schema, and with {@code list_files_only} it rewrites the
     * relation into a {@link com.starrocks.sql.ast.ValuesRelation} before {@code Authorizer.check()} ever runs --
     * so a subsequently denied REMAP LOGICAL statement could otherwise still perform external I/O. REMAP LOGICAL
     * only rewrites references to catalog/logical tables, so no table function is a supported input; this fails
     * closed rather than defining a whitelist for a case that has no legitimate use here.
     */
    private static void rejectTableFunctions(QueryStatement queryStmt) {
        new TableFunctionRejectionVisitor().visit(queryStmt);
    }

    private static final class TableFunctionRejectionVisitor extends RawAstSelectTraverser {
        @Override
        public Void visit(ParseNode node, Void context) {
            return node == null ? null : node.accept(this, context);
        }

        @Override
        public Void visitTableFunction(TableFunctionRelation node, Void context) {
            reject(node.getFunctionName());
            return super.visitTableFunction(node, context);
        }

        @Override
        public Void visitFileTableFunction(FileTableFunctionRelation node, Void context) {
            reject(new FunctionName(FileTableFunctionRelation.IDENTIFIER));
            return null;
        }

        private void reject(FunctionName fnName) {
            String normalized = fnName == null ? null : fnName.getFunction().toLowerCase(Locale.ROOT);
            throw new SemanticException("%s does not support table function: %s",
                    STATEMENT_NAME, normalized == null ? "<unknown>" : normalized);
        }
    }

    private static void validateParsedTopLevelLimit(RemapLogicalStmt statement) {
        long maxLimit = Config.validate_max_limit;
        if (maxLimit <= 0) {
            return;
        }
        QueryRelation queryRelation = statement.getQueryStmt().getQueryRelation();
        LimitElement limit = queryRelation.getLimit();
        if (limit == null) {
            throw new SemanticException(
                    "%s requires an explicit top-level LIMIT clause with value no greater than %d",
                    STATEMENT_NAME, maxLimit);
        }
        if (limit.getLimitExpr().isLiteral() && limit.getLimit() > maxLimit) {
            throw new SemanticException("%s top-level LIMIT must be no greater than %d, but was %d",
                    STATEMENT_NAME, maxLimit, limit.getLimit());
        }
    }

    private static void rejectStructExtensions(RemapLogicalStmt statement) {
        for (CelostarSchemaExtensionSpec extension : statement.getVirtualExtensions()) {
            if (extension.type().isStructType()) {
                throw new SemanticException("%s does not support STRUCT extension columns: %s",
                        STATEMENT_NAME, extension.column());
            }
        }
    }

    private static void validateTopLevelLimitValue(RemapLogicalStmt statement) {
        long maxLimit = Config.validate_max_limit;
        if (maxLimit <= 0) {
            return;
        }
        LimitElement limit = statement.getQueryStmt().getQueryRelation().getLimit();
        if (limit == null || !limit.hasLimit()) {
            throw new SemanticException(
                    "%s requires an explicit top-level LIMIT clause with value no greater than %d",
                    STATEMENT_NAME, maxLimit);
        }
        if (limit.getLimit() > maxLimit) {
            throw new SemanticException("%s top-level LIMIT must be no greater than %d, but was %d",
                    STATEMENT_NAME, maxLimit, limit.getLimit());
        }
    }

    private static RemappingSet resolveRemappings(RemapLogicalStmt statement, ConnectContext connectContext) {
        RemappingSet remappingSet = new RemappingSet();

        for (RemapLogicalStmt.TableMapping mapping : statement.getTableMappings()) {
            TableName sourceTable = LogicalSchemaExtensionAnalyzer.pathToTableName(
                    mapping.tablePath(), "table mapping source path");
            sourceTable.normalization(connectContext);
            if (remappingSet.hasTableMapping(sourceTable)) {
                throw new SemanticException("Duplicate REMAP LOGICAL table mapping: %s", sourceTable.toString());
            }
            remappingSet.addTableMapping(sourceTable, mapping.targetTable());
        }

        for (RemapLogicalStmt.ColumnMapping mapping : statement.getColumnMappings()) {
            TableName sourceTable = LogicalSchemaExtensionAnalyzer.pathToTableName(
                    mapping.tablePath(), "column mapping source table path");
            sourceTable.normalization(connectContext);
            if (remappingSet.hasColumnMapping(sourceTable, mapping.column())) {
                throw new SemanticException("Duplicate REMAP LOGICAL column mapping: %s.%s",
                        sourceTable.toString(), mapping.column());
            }
            remappingSet.addColumnMapping(sourceTable, mapping.column(), mapping.targetColumn());
        }

        return remappingSet;
    }

    private static void registerMappedVirtualTables(RemapLogicalStmt statement, CelostarSchemaExtension schemaExtension,
                                                    ConnectContext connectContext) {
        for (RemapLogicalStmt.TableMapping mapping : statement.getTableMappings()) {
            TableName sourceTable = LogicalSchemaExtensionAnalyzer.pathToTableName(
                    mapping.tablePath(), "table mapping source path");
            sourceTable.normalization(connectContext);
            schemaExtension.addTable(new ExtensionTableKey(sourceTable.getCatalog(), sourceTable.getDb(),
                    sourceTable.getTbl()));
        }
    }

    private static void validateUnmappedLogicalReferences(RemapLogicalStmt statement, ConnectContext connectContext) {
        try (CelostarExtensionScope ignored = CelostarExtensionScope.clear(connectContext)) {
            Map<TableName, Table> queryTables = AnalyzerUtils.collectAllTable(statement.getQueryStmt());
            Map<TableName, Set<String>> tableColumns =
                    AnalyzerUtils.collectAllSelectTableColumns(statement.getQueryStmt());
            expandTableStars(tableColumns, queryTables, connectContext);

            RemappingSet remappingSet = statement.getRemappingSet();
            validateTableMappingsResolve(queryTables, remappingSet, connectContext);
            validateColumnMappingsResolve(tableColumns, remappingSet, connectContext);
        }
    }

    private static void validateTableMappingsResolve(Map<TableName, Table> queryTables, RemappingSet remappingSet,
                                                      ConnectContext connectContext) {
        for (TableName tableName : queryTables.keySet()) {
            TableName normalizedTableName = normalizeTableName(tableName, connectContext);
            Table realTable = getRealCatalogTable(normalizedTableName, connectContext);
            if (realTable == null && !remappingSet.hasTableMapping(normalizedTableName)) {
                throw new SemanticException(
                        "Logical table '%s' is not in the catalog and has no REMAP LOGICAL table mapping",
                        normalizedTableName.toString());
            }
        }
    }

    private static void validateColumnMappingsResolve(Map<TableName, Set<String>> tableColumns,
                                                       RemappingSet remappingSet, ConnectContext connectContext) {
        for (Map.Entry<TableName, Set<String>> entry : tableColumns.entrySet()) {
            TableName normalizedTableName = normalizeTableName(entry.getKey(), connectContext);
            Table realTable = getRealCatalogTable(normalizedTableName, connectContext);
            for (String columnName : entry.getValue()) {
                if (realTable == null) {
                    if (!remappingSet.hasColumnMapping(normalizedTableName, columnName)) {
                        throw new SemanticException(
                                "Logical column '%s.%s' is not in the catalog and has no REMAP LOGICAL column mapping",
                                normalizedTableName.toString(), columnName);
                    }
                } else if (realTable.getColumn(columnName) == null &&
                        !remappingSet.hasColumnMapping(normalizedTableName, columnName)) {
                    throw new SemanticException(
                            "Logical column '%s.%s' is not in the catalog and has no REMAP LOGICAL column mapping",
                            normalizedTableName.toString(), columnName);
                }
            }
        }
    }

    private static void expandTableStars(Map<TableName, Set<String>> tableToColumns,
                                         Map<TableName, Table> queryTables,
                                         ConnectContext connectContext) {
        for (Map.Entry<TableName, Set<String>> entry : tableToColumns.entrySet()) {
            expandStarForEntry(entry, queryTables, connectContext);
        }
    }

    private static void expandStarForEntry(Map.Entry<TableName, Set<String>> entry,
                                           Map<TableName, Table> queryTables, ConnectContext connectContext) {
        Set<String> columns = entry.getValue();
        if (!columns.contains("*")) {
            return;
        }
        Table table = findTable(entry.getKey(), queryTables, connectContext);
        if (table == null) {
            return;
        }
        columns.remove("*");
        Set<String> expandedColumns = new HashSet<>();
        for (Column column : table.getBaseSchema()) {
            expandedColumns.add(column.getName());
        }
        columns.addAll(expandedColumns);
    }

    private static Table findTable(TableName tableName, Map<TableName, Table> queryTables,
                                   ConnectContext connectContext) {
        Table table = queryTables.get(tableName);
        if (table != null) {
            return table;
        }
        TableName normalizedTableName = normalizeTableName(tableName, connectContext);
        for (Map.Entry<TableName, Table> entry : queryTables.entrySet()) {
            if (normalizeTableName(entry.getKey(), connectContext).equals(normalizedTableName)) {
                return entry.getValue();
            }
        }
        return null;
    }

    private static Table getRealCatalogTable(TableName tableName, ConnectContext connectContext) {
        com.starrocks.server.MetadataMgr metadataManager = GlobalStateMgr.getCurrentState().getMetadataMgr();
        Table table = metadataManager.getTable(connectContext, tableName.getCatalog(), tableName.getDb(),
                tableName.getTbl());
        if (table != null) {
            return table;
        }
        // Validation runs after removing statement-scoped extensions; still accept case-only physical table matches.
        for (String candidateTableName : metadataManager.listTableNames(connectContext, tableName.getCatalog(),
                tableName.getDb())) {
            if (!candidateTableName.equals(tableName.getTbl()) &&
                    candidateTableName.equalsIgnoreCase(tableName.getTbl())) {
                return metadataManager.getTable(connectContext, tableName.getCatalog(), tableName.getDb(),
                        candidateTableName);
            }
        }
        return null;
    }

    private static TableName normalizeTableName(TableName tableName, ConnectContext connectContext) {
        TableName normalizedTableName = new TableName(tableName.getCatalog(), tableName.getDb(), tableName.getTbl(),
                tableName.getPos());
        normalizedTableName.normalization(connectContext);
        return normalizedTableName;
    }
}
