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
import com.starrocks.authorization.PrivilegeType;
import com.starrocks.catalog.Table;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.VirtualExtensionTable;
import com.starrocks.sql.analyzer.celonis.CelostarSchemaExtensionResolver;

import java.util.Optional;

public final class ExplainInputColumnsPrivilegeSupport {
    private ExplainInputColumnsPrivilegeSupport() {
    }

    public static boolean allowVirtualTableAction(ConnectContext context, TableName tableName,
                                                  PrivilegeType privilegeType) {
        CelostarExtensionSet celostarExtensions = context.getCelostarExtensions();
        if (!PrivilegeType.SELECT.equals(privilegeType) || celostarExtensions == null) {
            return false;
        }
        // An extension scope is installed for the whole statement even when it resolved to nothing, so most callers
        // arrive here for ordinary tables. MetadataMgr#getTable only synthesizes a VirtualExtensionTable for a table
        // the extension set has entries for, so anything else cannot be one and needs no lookup to rule out.
        if (!celostarExtensions.getSchemaExtension().hasExtensionsFor(tableName.getCatalog(), tableName.getDb(),
                tableName.getTbl())) {
            return false;
        }

        Optional<Table> table = Optional.ofNullable(
                CelostarSchemaExtensionResolver.resolveExtensionTable(context, tableName));
        return table.isPresent() && table.get() instanceof VirtualExtensionTable;
    }

    public static boolean isVirtualColumnAction(ConnectContext context, TableName tableName,
                                                String column, PrivilegeType privilegeType) {
        CelostarExtensionSet celostarExtensions = context.getCelostarExtensions();
        if (!PrivilegeType.SELECT.equals(privilegeType) || celostarExtensions == null) {
            return false;
        }

        // Membership first, deliberately. ColumnPrivilege calls this once per scanned column whenever the catalog uses
        // an external access controller, and the statement's extension scope is installed even when it resolved to
        // nothing -- so without this the table would be resolved for every ordinary column in the query, which on an
        // external catalog can mean a connector round trip each. A column the set does not claim is not exempt no
        // matter what the table turns out to be, so returning early cannot change any answer.
        CelostarSchemaExtension schemaExtension = celostarExtensions.getSchemaExtension();
        if (!schemaExtension.hasVirtualColumnFor(tableName.getCatalog(), tableName.getDb(), tableName.getTbl(),
                column)) {
            return false;
        }

        // Intentionally redundant: resolveValidated() is the authoritative layer that excludes existing columns from
        // the extension set, and the set installed here is the one it produced -- this check is not a substitute for
        // that. It exists as a second line of defense for extension sets that reach the ConnectContext by some other
        // path, so it resolves tables exactly as the resolver does (temporary tables included): a guard that resolved
        // fewer tables than the resolver would miss precisely the columns an unfiltered extension set could
        // misclassify as virtual. Only claimed columns get this far, so the cost falls on genuine candidates.
        Table table = CelostarSchemaExtensionResolver.resolveExtensionTable(context, tableName);
        return table == null || table instanceof VirtualExtensionTable || table.getColumn(column) == null;
    }
}
