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
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.QueryStatement;

import java.util.List;

public final class LogicalSchemaExtensionAnalyzer {
    private LogicalSchemaExtensionAnalyzer() {
    }

    public static void validateInnerQuery(QueryStatement queryStmt, String statementName) {
        if (queryStmt.isExplain()) {
            throw new SemanticException("Inner query of %s must not be an EXPLAIN", statementName);
        }
        if (queryStmt.hasOutFileClause()) {
            throw new SemanticException("INTO OUTFILE is not supported in %s", statementName);
        }
    }

    public static TableName pathToTableName(List<String> tablePathParts, String pathDescription) {
        if (tablePathParts == null || tablePathParts.isEmpty() || tablePathParts.size() > 3) {
            throw new SemanticException("Invalid %s; expect table, db.table, or catalog.db.table", pathDescription);
        }
        switch (tablePathParts.size()) {
            case 1:
                return new TableName(null, null, tablePathParts.get(0));
            case 2:
                return new TableName(null, tablePathParts.get(0), tablePathParts.get(1));
            case 3:
                return new TableName(tablePathParts.get(0), tablePathParts.get(1), tablePathParts.get(2));
            default:
                throw new SemanticException("Invalid %s", pathDescription);
        }
    }
}
