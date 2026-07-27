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

import com.google.common.collect.ImmutableSet;
import com.starrocks.analysis.Expr;
import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.FunctionName;
import com.starrocks.analysis.GroupingFunctionCallExpr;
import com.starrocks.analysis.ParseNode;
import com.starrocks.catalog.Function;
import com.starrocks.sql.ast.AstTraverser;
import com.starrocks.sql.ast.DdlStmt;
import com.starrocks.sql.ast.DmlStmt;
import com.starrocks.sql.ast.FileTableFunctionRelation;
import com.starrocks.sql.ast.NormalizedTableFunctionRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.ShowStmt;
import com.starrocks.sql.ast.StatementBase;
import com.starrocks.sql.ast.TableFunctionRelation;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Walks an analyzed inner query and collects whitelist violations without throwing. Every node is checked, and
 * traversal continues past a violation so a single VALIDATE reports all problems at once. Three kinds are reported:
 * <ul>
 *     <li>{@code statement} — a non-query statement type (defense-in-depth over the grammar), classified via the
 *     StarRocks {@code Dml/Ddl/Show} taxonomy;</li>
 *     <li>{@code ast_node} — a node whose exact class is not in {@link ValidateAllowedNodes};</li>
 *     <li>{@code function} — a scalar function <em>or</em> table-valued function whose name is not in the effective
 *     function whitelist.</li>
 * </ul>
 * Table functions ({@code unnest}, {@code generate_series}, {@code files()}, the Celonis {@code celonis_*} TVFs) are
 * not {@link FunctionCallExpr}s — they are relation nodes the stock {@link AstTraverser} does not descend into — so
 * they are handled by the relation overrides below rather than {@link #visitFunctionCall}.
 */
public final class ValidateWhitelistChecker extends AbstractRelationLeafFixupVisitor {
    // Relation nodes handled explicitly below; excluded from the generic ast_node check so a table function yields a
    // single clear `function` violation instead of also an `ast_node` row.
    private static final Set<Class<?>> TABLE_FUNCTION_RELATIONS = ImmutableSet.of(
            TableFunctionRelation.class, NormalizedTableFunctionRelation.class, FileTableFunctionRelation.class);

    private final Set<String> allowedFunctions;
    private final List<ValidateViolation> violations = new ArrayList<>();

    private ValidateWhitelistChecker(Set<String> allowedFunctions) {
        this.allowedFunctions = allowedFunctions;
    }

    public static List<ValidateViolation> check(QueryStatement queryStmt, Set<String> allowedFunctions) {
        ValidateWhitelistChecker checker = new ValidateWhitelistChecker(allowedFunctions);
        checker.visit(queryStmt);
        return checker.violations;
    }

    @Override
    public Void visit(ParseNode node, Void context) {
        if (node == null) {
            return null;
        }
        if (node instanceof StatementBase) {
            // Only a QueryStatement is ever allowed here, matched by exact class so a subclass cannot slip through.
            // A rejected statement is labelled via the Dml/Ddl/Show taxonomy; instanceof is appropriate there since
            // those base classes are abstract and have no single concrete leaf to match against.
            if (node.getClass() != QueryStatement.class) {
                violations.add(new ValidateViolation(ValidateViolation.KIND_STATEMENT,
                        classifyStatement((StatementBase) node)));
            }
        } else if (!TABLE_FUNCTION_RELATIONS.contains(node.getClass()) && !ValidateAllowedNodes.isAllowed(node)) {
            violations.add(new ValidateViolation(ValidateViolation.KIND_AST_NODE, node.getClass().getSimpleName()));
        }
        return node.accept(this, context);
    }

    @Override
    public Void visitFunctionCall(FunctionCallExpr node, Void context) {
        FunctionName fnName = node.getFnName();
        checkFunctionCall(fnName == null ? null : fnName.getFunction(), node.getFn());
        return super.visitFunctionCall(node, context);
    }

    @Override
    public Void visitGroupingFunctionCall(GroupingFunctionCallExpr node, Void context) {
        // GroupingFunctionCallExpr dispatches to visitGroupingFunctionCall rather than visitFunctionCall, so
        // GROUPING()/GROUPING_ID() would otherwise bypass the function whitelist entirely.
        FunctionName fnName = node.getFnName();
        checkFunctionCall(fnName == null ? null : fnName.getFunction(), null);
        return visitExpression(node, context);
    }

    @Override
    public Void visitTableFunction(TableFunctionRelation node, Void context) {
        FunctionName fnName = node.getFunctionName();
        checkFunctionCall(fnName == null ? null : fnName.getFunction(), node.getTableFunction());
        // AstTraverser does not descend into table-function arguments, so recurse explicitly.
        List<Expr> args = node.getChildExpressions();
        if (args == null && node.getFunctionParams() != null) {
            args = node.getFunctionParams().exprs();
        }
        if (args != null) {
            for (Expr arg : args) {
                visit(arg, context);
            }
        }
        return null;
    }

    @Override
    public Void visitNormalizedTableFunction(NormalizedTableFunctionRelation node, Void context) {
        // TABLE(fn(...)) is a cross join of a dual relation with the TableFunctionRelation on the right.
        if (node.getLeft() != null) {
            visit(node.getLeft(), context);
        }
        if (node.getRight() != null) {
            visit(node.getRight(), context);
        }
        return null;
    }

    @Override
    public Void visitFileTableFunction(FileTableFunctionRelation node, Void context) {
        // files() carries no function name; report under its identifier so it is subject to the same whitelist.
        checkFunctionCall(FileTableFunctionRelation.IDENTIFIER, null);
        return null;
    }

    private void checkFunctionCall(String rawName, Function resolvedFn) {
        String normalized = rawName == null ? null : rawName.toLowerCase(Locale.ROOT);
        if (normalized == null || !allowedFunctions.contains(normalized)) {
            violations.add(new ValidateViolation(ValidateViolation.KIND_FUNCTION,
                    normalized == null ? "<unknown>" : normalized));
            return;
        }
        // A database/global UDF can share a name with a trusted builtin (e.g. a UDF literally named "pi"); catalog
        // lookup falls back to it, so builtins in the hardcoded default whitelist must resolve to the real builtin.
        if (resolvedFn != null && resolvedFn.isUdf()
                && ValidateFunctionWhitelist.DEFAULT_ALLOWED_FUNCTIONS.contains(normalized)) {
            violations.add(new ValidateViolation(ValidateViolation.KIND_FUNCTION, normalized));
        }
    }

    private static String classifyStatement(StatementBase statement) {
        if (statement instanceof DmlStmt) {
            return "dml";
        }
        if (statement instanceof DdlStmt) {
            return "ddl";
        }
        if (statement instanceof ShowStmt) {
            return "show";
        }
        return statement.getClass().getSimpleName();
    }
}
