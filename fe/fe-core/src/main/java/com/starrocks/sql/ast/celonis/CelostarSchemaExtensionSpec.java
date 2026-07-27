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

package com.starrocks.sql.ast.celonis;

import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Type;
import com.starrocks.sql.parser.NodePosition;

import java.util.List;

/**
 * Parser-time declaration of a Celostar schema extension: a column that is defined elsewhere (not in the real table
 * metadata) which the inner query of an EXTENSIONS-carrying statement (EXPLAIN INPUT COLUMNS, VALIDATE) may reference.
 *
 * <p>Table path form: catalog.db.table OR db.table OR table, plus a typed column.
 */
public record CelostarSchemaExtensionSpec(List<String> tablePath, String column, Type type, NodePosition pos) {
    public TableName tableName() {
        if (tablePath == null || tablePath.isEmpty() || tablePath.size() > 3) {
            throw new IllegalArgumentException(
                    "Invalid extension table path; expect table, db.table, or catalog.db.table");
        }
        switch (tablePath.size()) {
            case 1:
                return new TableName(null, null, tablePath.get(0));
            case 2:
                return new TableName(null, tablePath.get(0), tablePath.get(1));
            case 3:
                return new TableName(tablePath.get(0), tablePath.get(1), tablePath.get(2));
            default:
                throw new IllegalArgumentException("Invalid extension table path");
        }
    }
}
