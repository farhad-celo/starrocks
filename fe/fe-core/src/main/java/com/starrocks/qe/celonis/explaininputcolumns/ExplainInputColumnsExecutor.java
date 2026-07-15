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

package com.starrocks.qe.celonis.explaininputcolumns;

import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.ScalarType;
import com.starrocks.qe.ConnectContext;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.qe.ShowResultSetMetaData;
import com.starrocks.sql.analyzer.AnalyzerUtils;
import com.starrocks.sql.ast.celonis.explaininputcolumns.ExplainInputColumnsStmt;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

public final class ExplainInputColumnsExecutor {
    private static final ShowResultSetMetaData META_DATA =
            ShowResultSetMetaData.builder()
                    .addColumn(new Column("Input Column", ScalarType.createVarchar(20)))
                    .build();

    private ExplainInputColumnsExecutor() {
    }

    /**
     * Build analyzed EXPLAIN INPUT COLUMNS output. The inner query must already have been analyzed.
     */
    public static ShowResultSet execute(ExplainInputColumnsStmt statement, ConnectContext connectContext) {
        Map<TableName, Set<String>> tableColumns =
                AnalyzerUtils.collectAllSelectTableColumns(statement.getQueryStmt());
        return new ShowResultSet(META_DATA, resultRows(tableColumns, connectContext));
    }

    private static List<List<String>> resultRows(Map<TableName, Set<String>> tableToColumns,
                                                 ConnectContext connectContext) {
        TreeSet<String> outputLines = new TreeSet<>();
        for (Map.Entry<TableName, Set<String>> entry : tableToColumns.entrySet()) {
            TableName tableName = normalizeTableName(entry.getKey(), connectContext);
            for (String columnName : entry.getValue()) {
                outputLines.add(tableName.getCatalog() + "." + tableName.getDb() + "." + tableName.getTbl() +
                        "." + columnName);
            }
        }

        List<List<String>> rows = new ArrayList<>(outputLines.size());
        for (String outputLine : outputLines) {
            rows.add(Collections.singletonList(outputLine));
        }
        return rows;
    }

    private static TableName normalizeTableName(TableName tableName, ConnectContext connectContext) {
        TableName normalizedTableName = new TableName(tableName.getCatalog(), tableName.getDb(), tableName.getTbl());
        normalizedTableName.normalization(connectContext);
        return normalizedTableName;
    }
}
