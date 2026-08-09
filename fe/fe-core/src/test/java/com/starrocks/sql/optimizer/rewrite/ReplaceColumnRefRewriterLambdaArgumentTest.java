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

package com.starrocks.sql.optimizer.rewrite;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.ast.expression.BinaryType;
import com.starrocks.sql.optimizer.OptimizerContext;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.BinaryPredicateOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rule.transformation.materialization.OptExpressionDuplicator;
import com.starrocks.type.ArrayType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.VarcharType;
import mockit.Mocked;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Lambda-argument re-binding performed by the duplicating variant of {@link ReplaceColumnRefRewriter}.
 */
public class ReplaceColumnRefRewriterLambdaArgumentTest {
    private static final ArrayType ARRAY_INT = new ArrayType(IntegerType.INT);

    private ColumnRefFactory factory;

    @BeforeEach
    public void setUp() {
        factory = new ColumnRefFactory();
    }

    private ColumnRefOperator lambdaArg(String name) {
        return factory.create(name, IntegerType.INT, true, true);
    }

    /** array_map((args) -> body, arrayColumn) */
    private CallOperator arrayMap(List<ColumnRefOperator> args, ScalarOperator body, ScalarOperator arrayColumn) {
        LambdaFunctionOperator lambda = new LambdaFunctionOperator(args, body, body.getType());
        return new CallOperator(FunctionSet.ARRAY_MAP, ARRAY_INT, Lists.newArrayList(lambda, arrayColumn));
    }

    private LambdaFunctionOperator lambdaOf(ScalarOperator arrayMapCall) {
        return (LambdaFunctionOperator) arrayMapCall.getChild(0);
    }

    /** One rewriter per call, i.e. each call stands for a separate copy of the fragment. */
    private ScalarOperator duplicate(ScalarOperator expression) {
        return new ReplaceColumnRefRewriter(Maps.newHashMap(), false, factory).rewrite(expression);
    }

    @Test
    public void testArgumentIsReboundAndBodyRewired() {
        ColumnRefOperator arr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator newArr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator x = lambdaArg("x");
        // array_map(x -> x + 1, arr), copied into a fragment where arr was remapped
        CallOperator original = arrayMap(Lists.newArrayList(x),
                new CallOperator(FunctionSet.ADD, IntegerType.INT,
                        Lists.newArrayList(x, ConstantOperator.createInt(1))),
                arr);

        Map<ColumnRefOperator, ScalarOperator> operatorMap = Maps.newHashMap();
        operatorMap.put(arr, newArr);
        ScalarOperator copy = new ReplaceColumnRefRewriter(operatorMap, false, factory).rewrite(original);

        // the free column is substituted as usual
        assertEquals(newArr.getId(), ((ColumnRefOperator) copy.getChild(1)).getId());
        // and the bound argument gets a fresh id, with the body following it
        ColumnRefOperator newArg = lambdaOf(copy).getRefColumns().get(0);
        assertNotEquals(x.getId(), newArg.getId());
        assertEquals(newArg.getId(), ((ColumnRefOperator) lambdaOf(copy).getLambdaExpr().getChild(0)).getId());
        // the input expression is left alone
        assertEquals(x.getId(), lambdaOf(original).getRefColumns().get(0).getId());
        assertEquals(arr.getId(), ((ColumnRefOperator) original.getChild(1)).getId());
    }

    @Test
    public void testNewArgumentKeepsLambdaArgumentTypeAndSignature() {
        ColumnRefOperator x = factory.create("x", IntegerType.INT, false, true);
        CallOperator original = arrayMap(Lists.newArrayList(x), x, factory.create("arr", ARRAY_INT, true));

        ColumnRefOperator newArg = lambdaOf(duplicate(original)).getRefColumns().get(0);

        // losing LAMBDA_ARGUMENT would make the argument report itself as a used column
        assertEquals(OperatorType.LAMBDA_ARGUMENT, newArg.getOpType());
        assertTrue(newArg.getUsedColumns().isEmpty());
        assertEquals("x", newArg.getName());
        assertEquals(IntegerType.INT, newArg.getType());
        assertFalse(newArg.isNullable());
    }

    @Test
    public void testTwoCopiesGetDistinctArgumentIds() {
        ColumnRefOperator arr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator x = lambdaArg("x");
        CallOperator original = arrayMap(Lists.newArrayList(x), x, arr);

        // this is what plan duplication does: one fragment copied into two
        int firstId = lambdaOf(duplicate(original)).getRefColumns().get(0).getId();
        int secondId = lambdaOf(duplicate(original)).getRefColumns().get(0).getId();

        assertNotEquals(firstId, secondId);
        assertNotEquals(x.getId(), firstId);
        assertNotEquals(x.getId(), secondId);
    }

    @Test
    public void testOneCopyKeepsABindingConsistentAcrossExpressions() {
        ColumnRefOperator arr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator x = lambdaArg("x");
        // one rewriter handles every expression of a copy, so the same lambda can be reached twice, e.g.
        // from a projection value and from a predicate
        CallOperator first = arrayMap(Lists.newArrayList(x), x, arr);
        CallOperator second = arrayMap(Lists.newArrayList(x), x, arr);

        ReplaceColumnRefRewriter rewriter = new ReplaceColumnRefRewriter(Maps.newHashMap(), false, factory);
        LambdaFunctionOperator firstCopy = lambdaOf(rewriter.rewrite(first));
        LambdaFunctionOperator secondCopy = lambdaOf(rewriter.rewrite(second));

        // one copy, one binding: both must land on the same new id, or the two stop being structurally equal
        // and sub-expression reuse and MV equation matching no longer see them as the same lambda
        assertEquals(firstCopy.getRefColumns().get(0).getId(), secondCopy.getRefColumns().get(0).getId());
        assertNotEquals(x.getId(), firstCopy.getRefColumns().get(0).getId());
        assertEquals(firstCopy, secondCopy);
    }

    @Test
    public void testNestedLambdaSeesReboundOuterArgument() {
        ColumnRefOperator outerArr = factory.create("arr1", ARRAY_INT, true);
        ColumnRefOperator innerArr = factory.create("arr2", ARRAY_INT, true);
        ColumnRefOperator x = lambdaArg("x");
        ColumnRefOperator y = lambdaArg("y");
        // array_map(x -> array_map(y -> x + y, arr2), arr1)
        CallOperator inner = arrayMap(Lists.newArrayList(y),
                new CallOperator(FunctionSet.ADD, IntegerType.INT, Lists.newArrayList(x, y)), innerArr);
        CallOperator original = arrayMap(Lists.newArrayList(x), inner, outerArr);

        ScalarOperator copy = duplicate(original);

        ColumnRefOperator newX = lambdaOf(copy).getRefColumns().get(0);
        ScalarOperator copiedInner = lambdaOf(copy).getLambdaExpr();
        ColumnRefOperator newY = lambdaOf(copiedInner).getRefColumns().get(0);
        ScalarOperator innerBody = lambdaOf(copiedInner).getLambdaExpr();

        assertNotEquals(x.getId(), newX.getId());
        assertNotEquals(y.getId(), newY.getId());
        assertNotEquals(newX.getId(), newY.getId());
        // the inner body must bind x to the OUTER lambda's new ref, and y to its own
        assertEquals(newX.getId(), ((ColumnRefOperator) innerBody.getChild(0)).getId());
        assertEquals(newY.getId(), ((ColumnRefOperator) innerBody.getChild(1)).getId());
    }

    @Test
    public void testMultipleArgumentsKeepTheirOrder() {
        ColumnRefOperator x = lambdaArg("x");
        ColumnRefOperator y = lambdaArg("y");
        // array_map((x, y) -> y, arr1, arr2) -- the body picks the second argument
        LambdaFunctionOperator lambda = new LambdaFunctionOperator(Lists.newArrayList(x, y), y, IntegerType.INT);
        CallOperator original = new CallOperator(FunctionSet.ARRAY_MAP, ARRAY_INT, Lists.newArrayList(lambda,
                factory.create("arr1", ARRAY_INT, true), factory.create("arr2", ARRAY_INT, true)));

        LambdaFunctionOperator copy = lambdaOf(duplicate(original));

        assertEquals(2, copy.getRefColumns().size());
        assertEquals("x", copy.getRefColumns().get(0).getName());
        assertEquals("y", copy.getRefColumns().get(1).getName());
        // still the second argument, so canReduce() keeps reporting index 2
        assertEquals(copy.getRefColumns().get(1).getId(), ((ColumnRefOperator) copy.getLambdaExpr()).getId());
        assertEquals(2, copy.canReduce());
    }

    @Test
    public void testHoistedCommonSubExpressionsAreReboundToo() {
        ColumnRefOperator x = lambdaArg("x");
        // stands for what ScalarOperatorsReuse hoists out of a lambda body: a plain ref bound to x + 1,
        // with the body reduced to references to that ref
        ColumnRefOperator hoisted = factory.create("common", IntegerType.INT, true);
        ScalarOperator hoistedExpr = new CallOperator(FunctionSet.ADD, IntegerType.INT,
                Lists.newArrayList(x, ConstantOperator.createInt(1)));
        LambdaFunctionOperator lambda = new LambdaFunctionOperator(Lists.newArrayList(x),
                new CallOperator(FunctionSet.MULTIPLY, IntegerType.INT, Lists.newArrayList(hoisted, hoisted)),
                IntegerType.INT);
        Map<ColumnRefOperator, ScalarOperator> columnRefMap = Maps.newHashMap();
        columnRefMap.put(hoisted, hoistedExpr);
        lambda.addColumnToExpr(columnRefMap);
        CallOperator original = new CallOperator(FunctionSet.ARRAY_MAP, ARRAY_INT,
                Lists.newArrayList(lambda, factory.create("arr", ARRAY_INT, true)));

        LambdaFunctionOperator copy = lambdaOf(duplicate(original));

        assertEquals(1, copy.getColumnRefMap().size());
        ColumnRefOperator newHoisted = copy.getColumnRefMap().keySet().iterator().next();
        ColumnRefOperator newArg = copy.getRefColumns().get(0);
        assertNotEquals(hoisted.getId(), newHoisted.getId());
        // the hoisted expression now refers to the new argument
        assertEquals(newArg.getId(),
                ((ColumnRefOperator) copy.getColumnRefMap().get(newHoisted).getChild(0)).getId());
        // and the body refers to the new hoisted ref
        assertEquals(newHoisted.getId(), ((ColumnRefOperator) copy.getLambdaExpr().getChild(0)).getId());
        assertEquals(newHoisted.getId(), ((ColumnRefOperator) copy.getLambdaExpr().getChild(1)).getId());
    }

    @Test
    public void testFreeColumnsStayVisibleAndArgumentsStayHidden() {
        ColumnRefOperator arr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator outer = factory.create("outer", IntegerType.INT, true);
        ColumnRefOperator x = lambdaArg("x");
        // array_map(x -> x = outer, arr): outer is free, x is bound
        CallOperator original = arrayMap(Lists.newArrayList(x),
                new BinaryPredicateOperator(BinaryType.EQ, x, outer), arr);

        ScalarOperator copy = duplicate(original);

        assertEquals(original.getUsedColumns(), copy.getUsedColumns());
        assertTrue(copy.getUsedColumns().contains(outer.getId()));
        assertTrue(copy.getUsedColumns().contains(arr.getId()));
        assertFalse(copy.getUsedColumns().contains(lambdaOf(copy).getRefColumns().get(0).getId()));
    }

    /**
     * The one production caller. Without this, reverting the two lines in OptExpressionDuplicator that pass
     * the factory would leave every other case in this class passing.
     */
    @Test
    public void testOptExpressionDuplicatorOptsIn(@Mocked OptimizerContext optimizerContext) {
        // The source id space, standing in for an MV's factory: arr is id 1, the lambda argument id 2.
        ColumnRefFactory sourceFactory = new ColumnRefFactory();
        ColumnRefOperator sourceArr = sourceFactory.create("arr", ARRAY_INT, true);
        ColumnRefOperator sourceArg = sourceFactory.create("x", IntegerType.INT, true, true);
        CallOperator arrayMap = arrayMap(Lists.newArrayList(sourceArg), sourceArg, sourceArr);

        // The target id space has independently given the argument's id to another column of another type,
        // which is the default rather than a coincidence, since every factory counts up from 1.
        ColumnRefFactory targetFactory = new ColumnRefFactory();
        targetFactory.create("some_text", VarcharType.VARCHAR, true);
        ColumnRefOperator targetArr = targetFactory.create("arr", ARRAY_INT, true);
        assertEquals(sourceArg.getId(), targetArr.getId(), "test setup: ids must overlap to be meaningful");

        OptExpressionDuplicator duplicator = new OptExpressionDuplicator(targetFactory, optimizerContext);
        // What the plan walk records for the free column before rewriting expressions; the map is the live
        // one the rewriter reads through.
        duplicator.getColumnMapping().put(sourceArr, targetArr);

        ScalarOperator copy = duplicator.rewriteAfterDuplicate(arrayMap);

        assertEquals(targetArr.getId(), ((ColumnRefOperator) copy.getChild(1)).getId());
        ColumnRefOperator copiedArg = lambdaOf(copy).getRefColumns().get(0);
        int id = copiedArg.getId();
        assertTrue(id >= 1 && id <= targetFactory.getColumnRefs().size(),
                "lambda argument id " + id + " was never allocated by the target factory, so it leaked in "
                        + "from the source factory");
        assertEquals(OperatorType.LAMBDA_ARGUMENT, targetFactory.getColumnRef(id).getOpType(),
                "the target factory holds id " + id + " as " + targetFactory.getColumnRef(id).getName() + " "
                        + targetFactory.getColumnRef(id).getType() + ", not as a lambda argument");
    }

    @Test
    public void testSubstitutingVariantKeepsLambdaArguments() {
        // the constructors without a factory must stay a pure substitution: no re-binding
        ColumnRefOperator arr = factory.create("arr", ARRAY_INT, true);
        ColumnRefOperator x = lambdaArg("x");
        CallOperator original = arrayMap(Lists.newArrayList(x), x, arr);

        ScalarOperator rewritten = new ReplaceColumnRefRewriter(Maps.newHashMap()).rewrite(original);

        assertEquals(x.getId(), lambdaOf(rewritten).getRefColumns().get(0).getId());
    }
}
