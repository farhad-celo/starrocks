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

package com.starrocks.qe.celonis.validate;

import com.starrocks.catalog.Column;
import com.starrocks.catalog.ScalarType;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.qe.ShowResultSetMetaData;
import com.starrocks.sql.analyzer.celonis.validate.ValidateFunctionWhitelist;
import com.starrocks.sql.analyzer.celonis.validate.ValidateViolation;
import com.starrocks.sql.analyzer.celonis.validate.ValidateWhitelistChecker;
import com.starrocks.sql.ast.celonis.validate.ValidateStmt;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * Runs the VALIDATE whitelist check over the already-analyzed inner query and formats the result.
 *
 * <p>The result set has two columns, {@code Violation} and {@code Detail}. When the query is clean it returns a single
 * {@code status / VALID} row; otherwise one row per violation, for the checks this class performs itself
 * (AST-node, function, and statement-kind violations against the analyzed query). It never throws for those.
 *
 * <p>Table-function whitelist violations are not among them: {@link com.starrocks.sql.analyzer.celonis.validate
 * .ValidateAnalyzer} rejects disallowed table functions with a {@code SemanticException} during analysis, before
 * this executor ever runs, so they surface as an analysis error rather than a violation row.
 */
public final class ValidateExecutor {
    private static final ShowResultSetMetaData META_DATA =
            ShowResultSetMetaData.builder()
                    .addColumn(new Column("Violation", ScalarType.createVarchar(32)))
                    .addColumn(new Column("Detail", ScalarType.createVarchar(256)))
                    .build();

    private ValidateExecutor() {
    }

    public static ShowResultSet execute(ValidateStmt statement) {
        List<ValidateViolation> violations = ValidateWhitelistChecker.check(statement.getQueryStmt(),
                ValidateFunctionWhitelist.effectiveAllowedFunctions());
        return new ShowResultSet(META_DATA, resultRows(violations));
    }

    private static List<List<String>> resultRows(List<ValidateViolation> violations) {
        List<List<String>> rows = new ArrayList<>();
        if (violations.isEmpty()) {
            rows.add(List.of("status", "VALID"));
            return rows;
        }
        violations.stream()
                .sorted(Comparator.comparing(ValidateViolation::kind).thenComparing(ValidateViolation::detail))
                .distinct()
                .forEach(violation -> rows.add(List.of(violation.kind(), violation.detail())));
        return rows;
    }
}
