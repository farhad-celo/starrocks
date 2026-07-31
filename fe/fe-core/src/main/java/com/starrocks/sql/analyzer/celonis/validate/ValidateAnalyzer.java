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

package com.starrocks.sql.analyzer.celonis.validate;

import com.starrocks.analysis.FunctionName;
import com.starrocks.analysis.LimitElement;
import com.starrocks.analysis.ParseNode;
import com.starrocks.common.Config;
import com.starrocks.qe.ConnectContext;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;
import com.starrocks.sql.analyzer.Analyzer;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.analyzer.celonis.CelostarExtensionScope;
import com.starrocks.sql.analyzer.celonis.CelostarSchemaExtensionResolver;
import com.starrocks.sql.analyzer.celonis.RawAstSelectTraverser;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.FileTableFunctionRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.celonis.validate.ValidateStmt;

import java.util.Locale;
import java.util.Set;

/**
 * Analyzes the inner query of a VALIDATE statement with the declared schema extensions installed, so that columns
 * defined elsewhere resolve and the query is confirmed well-formed. The whitelist check itself runs later, in the
 * executor, over the analyzed query.
 */
public final class ValidateAnalyzer {
    private ValidateAnalyzer() {
    }

    public static void analyze(ValidateStmt statement, ConnectContext connectContext) {
        if (statement.isExistQueryScopeHint()) {
            throw new SemanticException("Query-scope hints are not supported in VALIDATE");
        }
        validateInnerQuery(statement.getQueryStmt());
        CelostarSchemaExtension schemaExtension =
                CelostarSchemaExtensionResolver.resolveValidated(statement.getVirtualExtensions(), connectContext);
        // Install extensions only while the inner query is analyzed so normal statements keep the unextended catalog.
        try (CelostarExtensionScope ignored = CelostarExtensionScope.install(connectContext, schemaExtension)) {
            Analyzer.analyze(statement.getQueryStmt(), connectContext);
        }
        rejectExcessiveLimit(statement.getQueryStmt());
    }

    /**
     * The outer LIMIT (on {@code queryStmt.getQueryRelation()}, which for a UNION is the union-level limit, not a
     * per-branch one, and covers CTEs since those attach to this same relation) must be resolved to a literal before
     * it can be read, which only happens once {@link Analyzer#analyze} has run -- unlike the other checks in
     * {@link #validateInnerQuery}, this cannot run on the raw AST.
     */
    private static void rejectExcessiveLimit(QueryStatement queryStmt) {
        long maxLimit = Config.validate_max_limit;
        if (maxLimit <= 0) {
            return;
        }
        LimitElement limit = queryStmt.getQueryRelation().getLimit();
        if (limit == null || !limit.hasLimit()) {
            throw new SemanticException("VALIDATE requires a LIMIT clause (max " + maxLimit + ")");
        }
        if (limit.getLimit() > maxLimit) {
            throw new SemanticException("LIMIT " + limit.getLimit() + " exceeds the maximum of " + maxLimit +
                    " allowed by VALIDATE");
        }
    }

    private static void validateInnerQuery(QueryStatement queryStmt) {
        if (queryStmt.isExplain()) {
            throw new SemanticException("Inner query of VALIDATE must not be an EXPLAIN");
        }
        if (queryStmt.hasOutFileClause()) {
            throw new SemanticException("INTO OUTFILE is not supported in VALIDATE");
        }
        rawAstSafetyPass(queryStmt);
    }

    /**
     * Runs both the SELECT * and the table-function whitelist checks over the raw, unanalyzed AST, before
     * {@link Analyzer#analyze} runs. Table functions are resolved during analysis, and that resolution is not
     * side-effect-free: {@code files()} can reach out to external storage, and with {@code list_files_only} it
     * rewrites the {@link FileTableFunctionRelation} into a {@link com.starrocks.sql.ast.ValuesRelation} before the
     * whitelist check ever runs, erasing the evidence that a table function was used. Function names and arguments
     * are already available straight from the parser, so this fails closed before any resolution happens; the
     * post-analysis {@link ValidateWhitelistChecker} still covers everything else (including these same table
     * functions' arguments, once resolved).
     *
     * <p>{@link AstTraverser#visitSelect} traverses {@code getOutputExpression()}, which is only populated once
     * analysis has run and is null here, so a SELECT list expression (e.g. a scalar subquery containing
     * {@code FILES(...)}, or a nested {@code SELECT *}) would otherwise be invisible to this pass. This overrides
     * {@code visitSelect} to explicitly walk the raw {@code SelectListItem}s instead. Raw table-function arguments,
     * VALUES, and PIVOT payloads use the shared {@link CelostarAstTraverser}.
     */
    private static void rawAstSafetyPass(QueryStatement queryStmt) {
        new RawAstSafetyVisitor(ValidateFunctionWhitelist.effectiveAllowedFunctions()).visit(queryStmt);
    }

    private static final class RawAstSafetyVisitor extends RawAstSelectTraverser {
        private final Set<String> allowedFunctions;

        private RawAstSafetyVisitor(Set<String> allowedFunctions) {
            this.allowedFunctions = allowedFunctions;
        }

        @Override
        public Void visit(ParseNode node, Void context) {
            return node == null ? null : node.accept(this, context);
        }

        @Override
        protected void visitSelectListItem(SelectListItem item, Void context) {
            if (item.isStar()) {
                throw new SemanticException("SELECT * is not supported in VALIDATE");
            }
            super.visitSelectListItem(item, context);
        }

        @Override
        public Void visitTableFunction(TableFunctionRelation node, Void context) {
            rejectIfNotAllowed(node.getFunctionName());
            return super.visitTableFunction(node, context);
        }

        @Override
        public Void visitFileTableFunction(FileTableFunctionRelation node, Void context) {
            rejectIfNotAllowed(new FunctionName(FileTableFunctionRelation.IDENTIFIER));
            return null;
        }

        private void rejectIfNotAllowed(FunctionName fnName) {
            String normalized = fnName == null ? null : fnName.getFunction().toLowerCase(Locale.ROOT);
            if (normalized == null || !allowedFunctions.contains(normalized)) {
                throw new SemanticException("VALIDATE does not allow table function: " +
                        (normalized == null ? "<unknown>" : normalized));
            }
        }
    }
}
