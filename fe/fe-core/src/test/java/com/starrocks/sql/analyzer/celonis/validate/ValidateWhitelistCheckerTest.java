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

import com.starrocks.analysis.FunctionCallExpr;
import com.starrocks.analysis.FunctionName;
import com.starrocks.analysis.FunctionParams;
import com.starrocks.analysis.IntLiteral;
import com.starrocks.analysis.TableName;
import com.starrocks.catalog.Function;
import com.starrocks.catalog.TableFunction;
import com.starrocks.catalog.Type;
import com.starrocks.sql.ast.DropDbStmt;
import com.starrocks.sql.ast.FileTableFunctionRelation;
import com.starrocks.sql.ast.HdfsURI;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.SelectList;
import com.starrocks.sql.ast.SelectListItem;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.SetStmt;
import com.starrocks.sql.ast.ShowDbStmt;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.UpdateStmt;
import com.starrocks.sql.ast.ValuesRelation;
import com.starrocks.sql.parser.NodePosition;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Unit tests for {@link ValidateWhitelistChecker} that construct the AST directly (rather than going through a full
 * parse + analyze pass), so that a resolved {@link Function} can be attached to a call by hand — this is the
 * simplest way to simulate a same-named UDF shadowing a builtin without standing up a real UDF (jar/symbol).
 */
class ValidateWhitelistCheckerTest {

    private static QueryStatement queryWithCall(FunctionCallExpr call) {
        SelectListItem item = new SelectListItem(call, null);
        SelectRelation selectRelation = new SelectRelation(
                new SelectList(List.of(item), false), ValuesRelation.newDualRelation(), null, null, null);
        // getOutputExpression() is normally populated by the analyzer from selectList; set it directly since this
        // test builds the AST by hand without running a full analysis pass.
        selectRelation.setOutputExpr(List.of(call));
        return new QueryStatement(selectRelation);
    }

    private static FunctionCallExpr callResolvedTo(String name, Function fn) {
        FunctionCallExpr call = new FunctionCallExpr(name, Collections.emptyList());
        call.setFn(fn);
        return call;
    }

    private static QueryStatement queryWithTableFunction(TableFunctionRelation relation) {
        SelectListItem item = new SelectListItem(new IntLiteral(1), null);
        SelectRelation selectRelation = new SelectRelation(
                new SelectList(List.of(item), false), relation, null, null, null);
        selectRelation.setOutputExpr(List.of(new IntLiteral(1)));
        return new QueryStatement(selectRelation);
    }

    private static TableFunctionRelation tableFunctionResolvedTo(String name, TableFunction fn) {
        TableFunctionRelation relation = new TableFunctionRelation(name, new FunctionParams(Collections.emptyList()),
                null);
        relation.setTableFunction(fn);
        return relation;
    }

    private static QueryStatement queryWithFileTableFunction(FileTableFunctionRelation relation) {
        SelectListItem item = new SelectListItem(new IntLiteral(1), null);
        SelectRelation selectRelation = new SelectRelation(
                new SelectList(List.of(item), false), relation, null, null, null);
        selectRelation.setOutputExpr(List.of(new IntLiteral(1)));
        return new QueryStatement(selectRelation);
    }

    // classifyStatement's dml/ddl/show branches, and visit()'s null-node guard, are unreachable through any valid
    // VALIDATE grammar (defense-in-depth only) -- reflection is the only way to drive them directly.
    private static ValidateWhitelistChecker newChecker(Set<String> allowedFunctions) throws ReflectiveOperationException {
        Constructor<ValidateWhitelistChecker> ctor = ValidateWhitelistChecker.class.getDeclaredConstructor(Set.class);
        ctor.setAccessible(true);
        return ctor.newInstance(allowedFunctions);
    }

    @SuppressWarnings("unchecked")
    private static List<ValidateViolation> violationsOf(ValidateWhitelistChecker checker) throws ReflectiveOperationException {
        Field field = ValidateWhitelistChecker.class.getDeclaredField("violations");
        field.setAccessible(true);
        return (List<ValidateViolation>) field.get(checker);
    }

    @Test
    void udfShadowingDefaultBuiltinIsRejected() {
        // "pi" is a builtin in the default whitelist. A UDF that resolves under the same name (e.g. a same-named
        // global UDF with a different arity) must not be trusted just because the name matches.
        FunctionName name = new FunctionName(null, "pi");
        Function udf = new Function(name, new Type[0], Type.DOUBLE, false);
        udf.setLocation(new HdfsURI("hdfs://localhost/udf/fake.jar"));

        List<ValidateViolation> violations =
                ValidateWhitelistChecker.check(queryWithCall(callResolvedTo("pi", udf)), Set.of("pi"));
        assertTrue(violations.stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_FUNCTION)
                && v.detail().equalsIgnoreCase("pi")), violations.toString());
    }

    @Test
    void realBuiltinIsNotRejected() {
        FunctionName name = new FunctionName(null, "pi");
        Function builtin = new Function(name, new Type[0], Type.DOUBLE, false);

        List<ValidateViolation> violations =
                ValidateWhitelistChecker.check(queryWithCall(callResolvedTo("pi", builtin)), Set.of("pi"));
        assertFalse(violations.stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_FUNCTION)),
                violations.toString());
    }

    @Test
    void udtfShadowingDefaultBuiltinIsRejected() {
        // Same collision as udfShadowingDefaultBuiltinIsRejected, but for a table-valued function: a UDTF named
        // "pi" resolving in place of the (nonexistent) builtin table function of the same name must not be trusted.
        FunctionName name = new FunctionName(null, "pi");
        TableFunction udtf = new TableFunction(name, List.of("col"), List.of(), List.of(Type.DOUBLE));
        udtf.setLocation(new HdfsURI("hdfs://localhost/udf/fake.jar"));

        List<ValidateViolation> violations = ValidateWhitelistChecker.check(
                queryWithTableFunction(tableFunctionResolvedTo("pi", udtf)), Set.of("pi"));
        assertTrue(violations.stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_FUNCTION)
                && v.detail().equalsIgnoreCase("pi")), violations.toString());
    }

    @Test
    void disallowedFilesTableFunctionIsRejected() {
        FileTableFunctionRelation relation = new FileTableFunctionRelation(Map.of(), NodePosition.ZERO);
        List<ValidateViolation> violations = ValidateWhitelistChecker.check(
                queryWithFileTableFunction(relation), Set.of());
        assertTrue(violations.stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_FUNCTION)
                && v.detail().equalsIgnoreCase(FileTableFunctionRelation.IDENTIFIER)), violations.toString());
    }

    @Test
    void nullNodeIsIgnoredWithoutViolation() throws ReflectiveOperationException {
        ValidateWhitelistChecker checker = newChecker(Set.of());
        assertNull(checker.visit(null, null));
        assertTrue(violationsOf(checker).isEmpty());
    }

    @Test
    void dmlStatementIsClassifiedAsDml() throws ReflectiveOperationException {
        ValidateWhitelistChecker checker = newChecker(Set.of());
        UpdateStmt update = new UpdateStmt(new TableName("db", "t"), List.of(), null, null, null);

        checker.visit(update, null);

        assertTrue(violationsOf(checker).stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_STATEMENT)
                && v.detail().equals("dml")));
    }

    @Test
    void ddlStatementIsClassifiedAsDdl() throws ReflectiveOperationException {
        ValidateWhitelistChecker checker = newChecker(Set.of());
        DropDbStmt dropDb = new DropDbStmt(true, "db", false);

        checker.visit(dropDb, null);

        assertTrue(violationsOf(checker).stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_STATEMENT)
                && v.detail().equals("ddl")));
    }

    @Test
    void showStatementIsClassifiedAsShow() throws ReflectiveOperationException {
        ValidateWhitelistChecker checker = newChecker(Set.of());
        ShowDbStmt showDb = new ShowDbStmt("%");

        checker.visit(showDb, null);

        assertTrue(violationsOf(checker).stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_STATEMENT)
                && v.detail().equals("show")));
    }

    @Test
    void otherStatementFallsBackToClassName() throws ReflectiveOperationException {
        ValidateWhitelistChecker checker = newChecker(Set.of());
        SetStmt setStmt = new SetStmt(List.of());

        checker.visit(setStmt, null);

        assertTrue(violationsOf(checker).stream().anyMatch(v -> v.kind().equals(ValidateViolation.KIND_STATEMENT)
                && v.detail().equals("SetStmt")));
    }
}
