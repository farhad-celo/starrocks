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

package com.starrocks.catalog;

import com.google.common.base.Objects;
import com.google.common.base.Strings;
import com.starrocks.analysis.TableName;
import com.starrocks.sql.ast.TableRelation;

import java.util.List;

public class DeltaLakeView extends ConnectorView {
    private final RelationRewriteContext relationRewriteContext;

    public DeltaLakeView(long id, String catalogName, String dbName, String name,
                         List<Column> schema, String definition,
                         RelationRewriteContext relationRewriteContext) {
        super(id, catalogName, dbName, name, schema, definition, TableType.DELTALAKE_VIEW);
        this.relationRewriteContext = relationRewriteContext;
    }

    public static class RelationRewriteContext {
        private final String defaultCatalogName;
        private final String defaultDbName;
        private final String sourceCatalogName;

        public RelationRewriteContext(String defaultCatalogName, String defaultDbName, String sourceCatalogName) {
            this.defaultCatalogName = defaultCatalogName;
            this.defaultDbName = defaultDbName;
            this.sourceCatalogName = sourceCatalogName;
        }
    }

    @Override
    protected void formatRelations(List<TableRelation> tableRelations, List<String> cteRelationNames) {
        for (TableRelation tableRelation : tableRelations) {
            TableName name = tableRelation.getName();

            if (Strings.isNullOrEmpty(name.getCatalog()) || isSourceCatalog(name.getCatalog())) {
                name.setCatalog(relationRewriteContext.defaultCatalogName);
            }
            if (Strings.isNullOrEmpty(name.getDb())) {
                name.setDb(relationRewriteContext.defaultDbName);
            }
        }
    }

    private boolean isSourceCatalog(String catalogName) {
        return !Strings.isNullOrEmpty(relationRewriteContext.sourceCatalogName) &&
                catalogName.equalsIgnoreCase(relationRewriteContext.sourceCatalogName);
    }

    @Override
    public String getCatalogDBName() {
        return dbName;
    }

    @Override
    public String getCatalogTableName() {
        return name;
    }

    @Override
    public int hashCode() {
        return Objects.hashCode(catalogName, dbName, name);
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof DeltaLakeView)) {
            return false;
        }
        DeltaLakeView otherView = (DeltaLakeView) other;
        return Objects.equal(catalogName, otherView.catalogName) &&
                Objects.equal(dbName, otherView.dbName) &&
                Objects.equal(name, otherView.name);
    }
}
