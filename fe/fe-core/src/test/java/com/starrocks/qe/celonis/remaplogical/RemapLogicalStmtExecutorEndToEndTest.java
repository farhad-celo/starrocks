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

package com.starrocks.qe.celonis.remaplogical;

import com.starrocks.common.util.UUIDUtil;
import com.starrocks.qe.ShowResultSet;
import com.starrocks.qe.StmtExecutor;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.SqlParser;
import com.starrocks.sql.plan.PlanTestBase;
import org.junit.jupiter.api.Test;

import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Covers REMAP LOGICAL's statement-executor dispatch and explain-result proxying.
 *
 * <p>Database is "test"; table t0(v1,v2,v3 bigint).
 */
class RemapLogicalStmtExecutorEndToEndTest extends PlanTestBase {

    @Test
    void remappedSqlIsReturnedThroughStmtExecutor() throws Exception {
        connectContext.setQueryId(UUIDUtil.genUUID());
        StatementBase statement = SqlParser.parse("REMAP LOGICAL SELECT v1 FROM t0 LIMIT 10 " +
                "MAPPINGS (TABLE test.t0 TO physical_t0)", connectContext.getSessionVariable()).get(0);
        StmtExecutor executor = new StmtExecutor(connectContext, statement);
        executor.setProxy();

        executor.execute();

        ShowResultSet resultSet = executor.getProxyResultSet();
        assertNotNull(resultSet);
        String remappedSql = resultSet.getResultRows().stream()
                .map(row -> row.get(0))
                .collect(Collectors.joining("\n"));
        assertTrue(remappedSql.contains("FROM `physical_t0`"), remappedSql);
        assertTrue(remappedSql.contains("`physical_t0`.`v1`"), remappedSql);
        assertTrue(remappedSql.endsWith(" LIMIT 10"), remappedSql);
    }
}
