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

package com.starrocks.sql.ast.celonis.validate;

import com.starrocks.analysis.RedirectStatus;
import com.starrocks.sql.ast.AstVisitor;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.celonis.AbstractQueryWithVirtualExtensionsStmt;
import com.starrocks.sql.ast.celonis.CelostarSchemaExtensionSpec;
import com.starrocks.sql.parser.NodePosition;

import java.util.List;

/**
 * {@code VALIDATE <query> [EXTENSIONS (t.c : TYPE, ...)]} checks that the inner query only contains whitelisted AST
 * node types and whitelisted function calls (read-only, access-safe SQL). The EXTENSIONS clause lets the inner query
 * reference columns defined elsewhere, mirroring EXPLAIN INPUT COLUMNS.
 *
 * <p>{@link #getRedirectStatus} is {@link RedirectStatus#NO_FORWARD}, so a VALIDATE is always evaluated by the FE
 * that received it, using that FE's local {@code validate_allowed_functions} and {@code validate_max_limit} config
 * ({@link com.starrocks.sql.analyzer.celonis.validate.ValidateFunctionWhitelist},
 * {@link com.starrocks.sql.analyzer.celonis.validate.ValidateAnalyzer}). In a multi-FE cluster where that config
 * isn't kept identical across FEs, the same VALIDATE statement can report different results depending on which FE
 * it lands on. Operators who need deterministic results should keep the config consistent cluster-wide, or route
 * VALIDATE traffic to one authoritative FE.
 */
public class ValidateStmt extends AbstractQueryWithVirtualExtensionsStmt {
    public ValidateStmt(NodePosition pos, QueryStatement queryStmt,
                        List<CelostarSchemaExtensionSpec> virtualExtensions) {
        super(pos, queryStmt, virtualExtensions);
    }

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitValidateStatement(this, context);
    }
}
