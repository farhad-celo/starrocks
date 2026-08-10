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

package com.starrocks.sql.optimizer.rule.transformation.materialization;

import com.google.common.collect.Lists;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.optimizer.OptimizerContext;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.type.ArrayType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.VarcharType;
import mockit.Mocked;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * OptExpressionDuplicator can import a plan from another id space: its target factory is the one the copy
 * will belong to, while {@code duplicate(source, prevColumnRefFactory, ...)} names the factory the source
 * came from. MV rewrite does exactly that, handing over a cached MV plan built with the MV's own factory.
 *
 * <p>Every column ref is remapped into the target's id space -- except lambda arguments, which are excluded
 * from {@code getUsedColumns()} and so never enter {@code columnMapping}, and which live in
 * {@code refColumns} rather than among the children a rewriter walks. A lambda argument minted by the source
 * factory therefore survives into a copy owned by the target factory, where its id is very likely already
 * spoken for by an unrelated column of an unrelated type. That ambiguity is what corrupted column types in
 * #72832 / #73273, reached there by a stale cache across a re-plan rather than by a cross-factory import.
 *
 * <p>{@link MvTransparentRewriteLambdaArgIdTest} shows the same thing end to end through a real MV; this
 * pins the mechanism directly, with no cluster and no MV, by driving the duplicator's own rewriter.
 */
class OptExpressionDuplicatorLambdaArgTest {
    private static final ArrayType ARRAY_INT = new ArrayType(IntegerType.INT);

    @Test
    void lambdaArgumentIsReboundIntoTheTargetIdSpace(@Mocked OptimizerContext optimizerContext) {
        // The source plan's id space, standing in for an MV's factory: arr is id 1, the lambda argument id 2.
        ColumnRefFactory sourceFactory = new ColumnRefFactory();
        ColumnRefOperator sourceArr = sourceFactory.create("arr", ARRAY_INT, true);
        ColumnRefOperator sourceArg = sourceFactory.create("x", IntegerType.INT, true, true);
        assertEquals(OperatorType.LAMBDA_ARGUMENT, sourceArg.getOpType());
        // array_map(x -> x, arr)
        CallOperator arrayMap = new CallOperator(FunctionSet.ARRAY_MAP, ARRAY_INT, Lists.newArrayList(
                new LambdaFunctionOperator(Lists.newArrayList(sourceArg), sourceArg, IntegerType.INT),
                sourceArr));

        // The target id space, standing in for the query's factory. It has independently handed out the ids
        // the source used, to different columns of different types -- which is the default, not a
        // coincidence, since every factory counts up from 1.
        ColumnRefFactory targetFactory = new ColumnRefFactory();
        ColumnRefOperator targetText = targetFactory.create("some_text", VarcharType.VARCHAR, true);
        ColumnRefOperator targetArr = targetFactory.create("arr", ARRAY_INT, true);
        assertEquals(sourceArg.getId(), targetArr.getId(), "test setup: ids must overlap to be meaningful");
        assertNotEquals(sourceArg.getType(), targetArr.getType(), "test setup: the types must disagree");

        OptExpressionDuplicator duplicator = new OptExpressionDuplicator(targetFactory, optimizerContext);
        // What the plan walk would have recorded for the free column before rewriting expressions. The map is
        // the live one the rewriter reads through, which is how the duplicator itself fills it in mid-walk.
        duplicator.getColumnMapping().put(sourceArr, targetArr);

        ScalarOperator copy = duplicator.rewriteAfterDuplicate(arrayMap);

        // The free column is remapped, as it always was.
        assertEquals(targetArr.getId(), ((ColumnRefOperator) copy.getChild(1)).getId());

        LambdaFunctionOperator copiedLambda = (LambdaFunctionOperator) copy.getChild(0);
        ColumnRefOperator copiedArg = copiedLambda.getRefColumns().get(0);
        // The body must follow whatever the argument became. Compare ids, not identity: rewrite() clones the
        // input, and LambdaFunctionOperator.clone() clones refColumns entry by entry and lambdaExpr
        // separately, so the declaration and a body reference to it are never the same object afterwards.
        assertEquals(copiedArg.getId(), ((ColumnRefOperator) copiedLambda.getLambdaExpr()).getId());
        assertEquals(OperatorType.LAMBDA_ARGUMENT, copiedArg.getOpType(),
                "the copied argument must still be a lambda argument, or it starts reporting itself as a "
                        + "used column");

        // The point of the test: the copy belongs to targetFactory, so every ref in it must be one
        // targetFactory owns. Before the fix the argument still carried sourceFactory's id 2, which
        // targetFactory holds as an unrelated ARRAY<INT> column.
        int id = copiedArg.getId();
        int allocated = targetFactory.getColumnRefs().size();
        assertTrue(id >= 1 && id <= allocated,
                "lambda argument id " + id + " was never allocated by the target factory (it allocated 1.."
                        + allocated + "), so it leaked in from the source factory");
        ColumnRefOperator owner = targetFactory.getColumnRef(id);
        assertEquals(OperatorType.LAMBDA_ARGUMENT, owner.getOpType(),
                "the target factory holds id " + id + " as " + owner.getName() + " " + owner.getType()
                        + ", not as a lambda argument: the source factory's argument id leaked into a plan "
                        + "owned by the target factory");
        assertEquals(copiedArg.getType(), owner.getType(),
                "the target factory holds id " + id + " with type " + owner.getType()
                        + " but the plan uses it as " + copiedArg.getType());
        assertEquals(targetText.getId(), 1, "sanity: the target factory's first id is untouched");
    }
}
