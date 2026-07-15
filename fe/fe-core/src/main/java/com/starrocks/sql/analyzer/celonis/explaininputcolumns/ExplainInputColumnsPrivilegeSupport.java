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
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.server.celonis.explaininputcolumns.VirtualExtensionTable;

import java.util.Optional;

public final class ExplainInputColumnsPrivilegeSupport {
    private ExplainInputColumnsPrivilegeSupport() {
    }

    public static boolean allowVirtualTableAction(ConnectContext context, TableName tableName,
                                                  PrivilegeType privilegeType) {
        if (!PrivilegeType.SELECT.equals(privilegeType) || context.getCelostarExtensions() == null) {
            return false;
        }
        Optional<Table> table = GlobalStateMgr.getCurrentState().getMetadataMgr().getTable(context, tableName);
        return table.isPresent() && table.get() instanceof VirtualExtensionTable;
    }

    public static boolean isVirtualColumnAction(ConnectContext context, TableName tableName,
                                                String column, PrivilegeType privilegeType) {
        CelostarExtensionSet celostarExtensions = context.getCelostarExtensions();
        if (!PrivilegeType.SELECT.equals(privilegeType) || celostarExtensions == null) {
            return false;
        }

        CelostarSchemaExtension schemaExtension = celostarExtensions.getSchemaExtension();
        return schemaExtension.hasVirtualColumnFor(tableName.getCatalog(), tableName.getDb(),
                tableName.getTbl(), column);
    }
}
