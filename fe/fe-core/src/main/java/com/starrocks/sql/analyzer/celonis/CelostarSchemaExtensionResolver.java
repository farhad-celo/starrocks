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

import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.Database;
import com.starrocks.catalog.Table;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.CatalogMgr;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.MetadataMgr;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.server.celonis.explaininputcolumns.VirtualExtensionTable;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

/**
 * Resolves parser-time {@link CelostarSchemaExtensionSpec} declarations into a {@link CelostarSchemaExtension}.
 *
 * <p>Shared by every statement that carries an EXTENSIONS clause (EXPLAIN INPUT COLUMNS, VALIDATE). Resolution happens
 * once, during analysis, via {@link #resolveValidated}; the result is recorded on the statement so that authorization
 * classifies references against exactly what analysis saw rather than resolving them again.
 */
public final class CelostarSchemaExtensionResolver {
    private CelostarSchemaExtensionResolver() {
    }

    /**
     * Resolve extensions for analysis, validating that catalogs/databases exist and that no duplicates are declared.
     * Declarations of columns that already exist are omitted from the resolved extension so those columns retain their
     * physical catalog and privilege semantics; any type declared for them is ignored, because the catalog column's own
     * type is what the query is analyzed against.
     */
    public static CelostarSchemaExtension resolveValidated(List<CelostarSchemaExtensionSpec> specs,
                                                           ConnectContext connectContext) {
        CelostarSchemaExtension schemaExtension = new CelostarSchemaExtension();
        Set<ExtensionColumnKey> seenExtensions = new HashSet<>();
        Map<ExtensionTableKey, Table> resolvedTables = new HashMap<>();
        MetadataMgr metadataManager = GlobalStateMgr.getCurrentState().getMetadataMgr();

        for (CelostarSchemaExtensionSpec spec : specs) {
            TableName tableName = tableName(spec);
            tableName.normalization(connectContext);

            ExtensionTableKey tableKey = new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(),
                    tableName.getTbl());
            if (!seenExtensions.add(new ExtensionColumnKey(tableKey, spec.column()))) {
                throw new SemanticException("Duplicate schema extension: %s.%s", tableName.getTbl(), spec.column());
            }

            if (!GlobalStateMgr.getCurrentState().getCatalogMgr().catalogExists(tableName.getCatalog())) {
                throw new SemanticException("Extension refers to unknown catalog: %s", tableName.getCatalog());
            }
            Database database = metadataManager.getDb(connectContext, tableName.getCatalog(), tableName.getDb());
            if (database == null) {
                throw new SemanticException("Extension refers to unknown database: %s.%s", tableName.getCatalog(),
                        tableName.getDb());
            }

            Table table = lookupTable(connectContext, tableName, database, resolvedTables);
            if (physicalColumn(table, spec.column()) != null) {
                // A declared type is ignored here rather than checked against the column. The restatement is dropped,
                // so the declared type never reaches the query -- the catalog column and its own type are what get
                // analyzed either way, which makes any disagreement inert.
                //
                // Checking it would not be free of consequence though: resolution runs before Authorizer.check (see
                // StatementPlanner#plan) and validates every spec, referenced by the inner query or not. Whether the
                // check fired would then be observable without holding any privilege on the table, and a caller could
                // read a column's type family off which of EXTENSIONS (db.t.c : INT), (: BIGINT), (: VARCHAR) ...
                // avoided the error -- while a column that does not exist accepts all of them. Omitting the catalog
                // type from the message would not have closed that channel; only not comparing does.
                continue;
            }

            schemaExtension.add(tableKey, spec.column(), spec.type());
        }
        return schemaExtension;
    }

    /**
     * Resolve the table behind an extension declaration the way {@link com.starrocks.sql.analyzer.QueryAnalyzer} does,
     * where a temporary table of the current session shadows a catalog table of the same name. Resolving only through
     * {@link MetadataMgr#getTable} would make "existing column" mean something narrower than it does during query
     * analysis, so a restatement of a temporary table's column would be treated as virtual and exempted from its
     * column-level privilege check.
     *
     * <p>Exposed so that every place deciding whether a column is real rather than virtual can agree on what "real"
     * means. Callers resolving many specs at once should prefer the memoized path used internally here.
     */
    public static Table resolveExtensionTable(ConnectContext connectContext, TableName tableName) {
        return resolveTable(connectContext, tableName, null);
    }

    /**
     * {@link #resolveExtensionTable} memoized per resolve call: a statement usually declares several extensions on the
     * same table, and a miss can cost a metastore round trip on an external catalog.
     *
     * @param database the already-resolved database for {@code tableName}, or null to resolve it on demand
     */
    private static Table lookupTable(ConnectContext connectContext, TableName tableName, Database database,
                                     Map<ExtensionTableKey, Table> resolvedTables) {
        ExtensionTableKey tableKey = new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(),
                tableName.getTbl());
        if (resolvedTables.containsKey(tableKey)) {
            return resolvedTables.get(tableKey);
        }
        Table table = resolveTable(connectContext, tableName, database);
        resolvedTables.put(tableKey, table);
        return table;
    }

    private static Table resolveTable(ConnectContext connectContext, TableName tableName, Database database) {
        Table table = temporaryTable(connectContext, tableName, database);
        if (table == null) {
            table = GlobalStateMgr.getCurrentState().getMetadataMgr().getTable(connectContext,
                    tableName.getCatalog(), tableName.getDb(), tableName.getTbl());
        }
        return table;
    }

    private static Table temporaryTable(ConnectContext connectContext, TableName tableName, Database database) {
        // Temporary tables only live in the internal catalog. Checking that first keeps the database lookup below off
        // the path for external catalogs, where it can be a connector round trip made solely to compute an argument
        // for a call that cannot succeed.
        if (connectContext == null || !CatalogMgr.isInternalCatalog(tableName.getCatalog())) {
            return null;
        }
        UUID sessionId = connectContext.getSessionId();
        if (sessionId == null) {
            // TemporaryTableMgr keys sessions in a ConcurrentHashMap, which rejects a null key.
            return null;
        }
        MetadataMgr metadataManager = GlobalStateMgr.getCurrentState().getMetadataMgr();
        Database resolvedDatabase = database != null ? database
                : metadataManager.getDb(connectContext, tableName.getCatalog(), tableName.getDb());
        if (resolvedDatabase == null) {
            return null;
        }
        return metadataManager.getTemporaryTable(sessionId, tableName.getCatalog(), resolvedDatabase.getId(),
                tableName.getTbl());
    }

    private static Column physicalColumn(Table table, String columnName) {
        // A VirtualExtensionTable is synthesized by MetadataMgr#getTable from an already-installed extension set, so
        // its columns are extensions rather than catalog columns. resolveValidated cannot see one today (it runs before
        // CelostarExtensionScope installs anything), but resolveExtensionTable is called from the privilege path with a
        // set installed, so treating it as "no real column" matters there.
        if (table == null || table instanceof VirtualExtensionTable) {
            return null;
        }
        return table.getColumn(columnName);
    }

    private static TableName tableName(CelostarSchemaExtensionSpec spec) {
        try {
            return spec.tableName();
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
