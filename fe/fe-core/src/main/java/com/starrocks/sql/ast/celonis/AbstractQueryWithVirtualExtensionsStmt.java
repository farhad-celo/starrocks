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

import com.google.common.collect.ImmutableList;
import com.starrocks.analysis.RedirectStatus;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.parser.NodePosition;

import java.util.List;

/**
 * Common shape shared by statements that wrap an inner {@link QueryStatement} together with a declared list of
 * {@link CelostarSchemaExtensionSpec} virtual columns (VALIDATE, EXPLAIN INPUT COLUMNS): both are always evaluated by
 * the FE that received them ({@link RedirectStatus#NO_FORWARD}), so that the declared extensions and any local
 * config used to interpret them stay consistent within a single request.
 */
public abstract class AbstractQueryWithVirtualExtensionsStmt extends StatementBase {
    private final QueryStatement queryStmt;
    private final List<CelostarSchemaExtensionSpec> virtualExtensions;

    protected AbstractQueryWithVirtualExtensionsStmt(NodePosition pos, QueryStatement queryStmt,
                                                      List<CelostarSchemaExtensionSpec> virtualExtensions) {
        super(pos);
        this.queryStmt = queryStmt;
        this.virtualExtensions = ImmutableList.copyOf(virtualExtensions);
    }

    public QueryStatement getQueryStmt() {
        return queryStmt;
    }

    public List<CelostarSchemaExtensionSpec> getVirtualExtensions() {
        return virtualExtensions;
    }

    @Override
    public RedirectStatus getRedirectStatus() {
        return RedirectStatus.NO_FORWARD;
    }
}
