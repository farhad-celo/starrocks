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

import com.starrocks.analysis.TableName;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;

/**
 * Installs EXPLAIN INPUT COLUMNS extensions while authorization walks the analyzed inner query.
 */
public final class ExplainInputColumnsAuthorizationScope implements AutoCloseable {
    private final ConnectContext context;
    private final CelostarExtensionSet previousCelostarExtensions;

    private ExplainInputColumnsAuthorizationScope(ConnectContext context, CelostarExtensionSet previousCelostarExtensions) {
        this.context = context;
        this.previousCelostarExtensions = previousCelostarExtensions;
    }

    public static ExplainInputColumnsAuthorizationScope install(ExplainInputColumnsStmt statement,
                                                               ConnectContext context) {
        CelostarExtensionSet previousCelostarExtensions = context.getCelostarExtensions();
        context.setCelostarExtensions(CelostarExtensionSet.ofSchemaExtension(toSchemaExtension(statement, context)));
        return new ExplainInputColumnsAuthorizationScope(context, previousCelostarExtensions);
    }

    @Override
    public void close() {
        context.setCelostarExtensions(previousCelostarExtensions);
    }

    private static CelostarSchemaExtension toSchemaExtension(ExplainInputColumnsStmt statement, ConnectContext context) {
        CelostarSchemaExtension schemaExtension = new CelostarSchemaExtension();
        for (ExplainInputColumnsStmt.VirtualExtension virtualExtension : statement.getVirtualExtensions()) {
            TableName tableName = virtualExtension.tableName();
            tableName.normalization(context);
            schemaExtension.add(new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(), tableName.getTbl()),
                    virtualExtension.column(), virtualExtension.type());
        }
        return schemaExtension;
    }
}
