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
import com.starrocks.catalog.Database;
import com.starrocks.catalog.Table;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.MetadataMgr;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;

import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Resolves parser-time {@link CelostarSchemaExtensionSpec} declarations into a {@link CelostarSchemaExtension}.
 *
 * <p>Shared by every statement that carries an EXTENSIONS clause (EXPLAIN INPUT COLUMNS, VALIDATE). Analysis uses
 * {@link #resolveValidated} which checks that each declared extension is well-formed and does not shadow a real column;
 * authorization runs later over the already-validated AST and uses {@link #resolveUnchecked}.
 */
public final class CelostarSchemaExtensionResolver {
    private CelostarSchemaExtensionResolver() {
    }

    /**
     * Resolve extensions for analysis, validating that catalogs/databases exist, no duplicates are declared, and no
     * extension shadows an existing real column.
     */
    public static CelostarSchemaExtension resolveValidated(List<CelostarSchemaExtensionSpec> specs,
                                                           ConnectContext connectContext) {
        CelostarSchemaExtension schemaExtension = new CelostarSchemaExtension();
        Set<ExtensionColumnKey> seenExtensions = new HashSet<>();
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

            Table table = metadataManager.getTable(connectContext, tableName.getCatalog(), tableName.getDb(),
                    tableName.getTbl());
            if (table != null && table.getColumn(spec.column()) != null) {
                throw new SemanticException("Extension column '%s' already exists on table '%s'", spec.column(),
                        tableName.getTbl());
            }

            schemaExtension.add(tableKey, spec.column(), spec.type());
        }
        return schemaExtension;
    }

    /**
     * Resolve extensions without existence validation, for use after analysis has already validated them (e.g. during
     * authorization).
     */
    public static CelostarSchemaExtension resolveUnchecked(List<CelostarSchemaExtensionSpec> specs,
                                                          ConnectContext connectContext) {
        CelostarSchemaExtension schemaExtension = new CelostarSchemaExtension();
        for (CelostarSchemaExtensionSpec spec : specs) {
            TableName tableName = spec.tableName();
            tableName.normalization(connectContext);
            schemaExtension.add(new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(), tableName.getTbl()),
                    spec.column(), spec.type());
        }
        return schemaExtension;
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
