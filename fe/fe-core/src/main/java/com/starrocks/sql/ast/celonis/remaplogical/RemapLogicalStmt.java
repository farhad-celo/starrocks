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

package com.starrocks.sql.ast.celonis.remaplogical;

import com.google.common.collect.ImmutableList;
import com.starrocks.sql.ast.AstVisitor;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.celonis.AbstractQueryWithVirtualExtensionsStmt;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;
import com.starrocks.sql.parser.NodePosition;

import java.util.List;

/**
 * Shares {@link AbstractQueryWithVirtualExtensionsStmt} with the other EXTENSIONS-carrying statements so that the
 * clause behaves identically across all of them -- including recording the analysis-time extension set for
 * authorization to reuse.
 */
public class RemapLogicalStmt extends AbstractQueryWithVirtualExtensionsStmt {
    private final List<TableMapping> tableMappings;
    private final List<ColumnMapping> columnMappings;
    private RemappingSet remappingSet;

    public RemapLogicalStmt(NodePosition pos,
                            QueryStatement queryStmt,
                            List<TableMapping> tableMappings,
                            List<ColumnMapping> columnMappings,
                            List<CelostarSchemaExtensionSpec> virtualExtensions) {
        super(pos, queryStmt, virtualExtensions);
        this.tableMappings = ImmutableList.copyOf(tableMappings);
        this.columnMappings = ImmutableList.copyOf(columnMappings);
    }

    public List<TableMapping> getTableMappings() {
        return tableMappings;
    }

    public List<ColumnMapping> getColumnMappings() {
        return columnMappings;
    }

    public RemappingSet getRemappingSet() {
        return remappingSet;
    }

    public void setRemappingSet(RemappingSet remappingSet) {
        this.remappingSet = remappingSet;
    }

    /**
     * Parser-time form: catalog.db.table OR db.table OR table, remapped to a table identifier.
     */
    public record TableMapping(List<String> tablePath, String targetTable, NodePosition pos) {}

    /**
     * Parser-time form: catalog.db.table.column OR db.table.column OR table.column, remapped to a column identifier.
     */
    public record ColumnMapping(List<String> tablePath, String column, String targetColumn, NodePosition pos) {}

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitRemapLogicalStatement(this, context);
    }
}
