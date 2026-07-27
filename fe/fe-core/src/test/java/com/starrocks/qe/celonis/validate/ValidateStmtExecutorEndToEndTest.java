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

import com.starrocks.common.util.UUIDUtil;
import com.starrocks.qe.QueryState;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.qe.StmtExecutor;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.ParsingException;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Drives VALIDATE through the real {@link StmtExecutor} entry point (rather than the direct
 * parse-analyze-{@link ValidateExecutor#execute} helper used elsewhere), so hint dispatch and result-set proxying get
 * exercised too.
 *
 * <p>Database is "test"; table t0(v1,v2,v3 bigint).
 */
class ValidateStmtExecutorEndToEndTest extends PlanTestBase {

    private StmtExecutor executeAsProxy(String sql) throws Exception {
        connectContext.setQueryId(UUIDUtil.genUUID());
        StatementBase parsedStatement = SqlParser.parse(sql, connectContext.getSessionVariable()).get(0);
        StmtExecutor executor = new StmtExecutor(connectContext, parsedStatement);
        executor.setProxy();
        executor.execute();
        return executor;
    }

    @Test
    void hintOnValidateIsRejectedThroughStmtExecutor() throws Exception {
        // StmtExecutor.execute() must skip processQueryScopeHint() for ValidateStmt, so the hint is caught by
        // ValidateAnalyzer's own check rather than executed before the whitelist ever sees it.
        StmtExecutor executor = executeAsProxy("VALIDATE SELECT /*+ SET_VAR(query_timeout=1) */ v1 FROM t0");
        assertEquals(QueryState.MysqlStateType.ERR, connectContext.getState().getStateType());
        assertTrue(connectContext.getState().getErrorMessage().contains("Query-scope hints are not supported"),
                connectContext.getState().getErrorMessage());
        assertNull(executor.getProxyResultSet());
    }

    @Test
    void validQueryReturnsValidThroughStmtExecutor() throws Exception {
        StmtExecutor executor = executeAsProxy("VALIDATE SELECT v1, v2 FROM t0 WHERE v3 > 0");
        ShowResultSet resultSet = executor.getProxyResultSet();
        List<List<String>> rows = resultSet.getResultRows();
        assertEquals(List.of(List.of("status", "VALID")), rows, rows.toString());
    }

    @Test
    void ddlAsInnerStatementNeverReachesStmtExecutor() {
        // The validateStatement grammar rule only accepts a queryStatement, so DDL can't be parsed as VALIDATE's
        // inner statement at all -- it fails before a ValidateStmt (and therefore a StmtExecutor) ever exists.
        assertThrows(ParsingException.class, () -> executeAsProxy("VALIDATE CREATE TABLE t2 (v1 BIGINT)"));
    }

    @Test
    void showAsInnerStatementNeverReachesStmtExecutor() {
        assertThrows(ParsingException.class, () -> executeAsProxy("VALIDATE SHOW TABLES"));
    }
}
