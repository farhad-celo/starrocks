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

import com.google.common.collect.ImmutableList;
import com.starrocks.common.Pair;
import com.starrocks.schema.MTable;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.Projection;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.plan.ExecPlan;
import com.starrocks.sql.plan.PlanTestBase;
import com.starrocks.thrift.TExplainLevel;
import com.starrocks.utframe.StarRocksAssert;
import com.starrocks.utframe.UtFrameUtils;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Companion to {@link com.starrocks.sql.plan.LambdaArgIdCollisionTest}, which covers lambda-argument ids
 * leaking across ColumnRefFactory instances on a re-plan (#72832 / #73273).
 *
 * <p>This covers the other route into the same state. OptExpressionDuplicator can import a plan from a
 * different id space: its target factory is the query's, while {@code duplicate(source, prevColumnRefFactory,
 * ...)} names the factory the source came from. MaterializedViewTransparentRewriteRule uses exactly that to
 * splice an MV's defined query plan into a query, passing {@code mvContext.getMvColumnRefFactory()} as the
 * source -- and that plan comes from {@code MvPlanContext.getLogicalPlan()}, a cached plan genuinely built
 * with the MV's own factory. Every column ref is remapped into the query's id space, but lambda arguments are
 * not: getUsedColumns() hides them so they never enter columnMapping, and refColumns is not a child so
 * walking children cannot reach it.
 *
 * <p>The invariant asserted here is ownership, not collision. Every ColumnRefOperator in a finished plan
 * should belong to the factory that owns the plan, meaning {@code queryFactory.getColumnRef(id)} agrees with
 * it. Asserting "no lambda-argument id is also held by a scalar column" instead would be a lottery: a foreign
 * id only collides when it happens to land on an id the query side also allocated *and* both refs survive
 * into the physical plan, so a green run could not distinguish "nothing leaked" from "a leak got lucky".
 * Ownership fails on a foreign id either way, and reports which column the query factory believes owns it --
 * the #72832 signature being a lambda argument whose id resolves to an unrelated column of another type.
 */
public class MvTransparentRewriteLambdaArgIdTest extends MVTestBase {
    private static MTable tableWithArray;

    @BeforeAll
    public static void beforeClass() throws Exception {
        MVTestBase.beforeClass();
        connectContext.getSessionVariable().setMaterializedViewRewriteMode("force");
        tableWithArray = new MTable("t_arr", "k1",
                ImmutableList.of(
                        "k1 INT",
                        "c1 string",
                        "c2 string",
                        "c3 string",
                        "c4 INT",
                        "c5 INT",
                        "arr ARRAY<INT>"
                ),
                "k1",
                ImmutableList.of(
                        "PARTITION `p1` VALUES LESS THAN ('3')",
                        "PARTITION `p2` VALUES LESS THAN ('6')",
                        "PARTITION `p3` VALUES LESS THAN ('9')"
                )
        );
    }

    private static final String QUERY = "select k1, c1, c2, mapped from mv_lambda";

    /**
     * transparent_mv_rewrite_mode forces the transparent path without depending on staleness heuristics;
     * refreshing only p1 leaves the MV partially fresh, which is what makes the rule splice the MV's defined
     * query plan (over the base table) into the query.
     */
    private void withPartiallyRefreshedLambdaMv(StarRocksAssert.ExceptionRunnable runner) throws Exception {
        starRocksAssert.withTable(tableWithArray, () -> {
            cluster.runSql("test", "insert into t_arr values " +
                    "(1,'a','b','c',1,1,[1,2,3]), (4,'d','e','f',2,2,[4,5,6]);");
            starRocksAssert.withMaterializedView("CREATE MATERIALIZED VIEW mv_lambda " +
                            " PARTITION BY (k1) " +
                            " DISTRIBUTED BY HASH(k1) " +
                            " REFRESH DEFERRED MANUAL " +
                            " PROPERTIES ( 'transparent_mv_rewrite_mode' = 'true' ) " +
                            " AS SELECT k1, c1, c2, array_map(x -> x + 1, arr) AS mapped FROM t_arr;",
                    () -> {
                        starRocksAssert.refreshMvPartition(
                                "REFRESH MATERIALIZED VIEW mv_lambda PARTITION START ('1') END ('3')");
                        runner.run();
                    });
        });
    }

    /**
     * Whether the ambiguous id is merely present or actually consulted. ScalarOperatorToExpr writes every
     * lambda argument into the projection-wide colRefToExpr and reads that same map back in
     * visitVariableReference, keyed by a ColumnRefOperator whose equals/hashCode are id-only, so one of the
     * two entries for the shared id wins and a later lookup gets the wrong type. Which one wins depends on
     * the order PlanFragmentBuilder assigns slots, so this executes the query rather than reasoning about it:
     * a type mismatch surfaces FE-side while building descriptors, as a SQLException.
     *
     * <p>Kept separate from the ownership test on purpose. That one fails before reaching execution on an
     * unfixed tree, which is exactly where this question needs answering.
     */
    @Test
    public void testQueryOverPartiallyRefreshedLambdaMvExecutes() throws Exception {
        withPartiallyRefreshedLambdaMv(() -> cluster.runSql("test", QUERY));
    }

    @Test
    public void testEveryColumnRefInThePlanIsOwnedByTheQueryFactory() throws Exception {
        withPartiallyRefreshedLambdaMv(() -> {
            String sql = QUERY;
            Pair<String, Pair<ExecPlan, String>> result =
                    UtFrameUtils.getFragmentPlanWithTrace(connectContext, sql, "MV");
            ExecPlan execPlan = result.second.first;
            String plan = execPlan.getExplainString(TExplainLevel.NORMAL);

            // Guards against a vacuous pass: the MV's defined query plan must really have been
            // duplicated in, and the lambda must have survived projection pruning.
            PlanTestBase.assertContains(plan, "t_arr");
            PlanTestBase.assertContains(plan, "array_map");

            ColumnRefFactory queryFactory = execPlan.getColumnRefFactory();
            Assertions.assertNotNull(queryFactory,
                    "ExecPlan carries no ColumnRefFactory, so ownership cannot be checked this way");
            int allocated = queryFactory.getColumnRefs().size();

            List<ColumnRefOperator> all = new ArrayList<>();
            collectColumnRefs(execPlan.getPhysicalPlan(), all);
            Assertions.assertFalse(all.isEmpty(), "collected no column refs, the plan walk is wrong");

            List<String> lambdaArgs = new ArrayList<>();
            List<String> unowned = new ArrayList<>();
            for (ColumnRefOperator ref : all) {
                int id = ref.getId();
                if (ref.getOpType() == OperatorType.LAMBDA_ARGUMENT) {
                    lambdaArgs.add(describe(ref));
                }
                if (id < 1 || id > allocated) {
                    unowned.add(describe(ref) + " -> the query factory never allocated id " + id
                            + " (it allocated 1.." + allocated + ")");
                    continue;
                }
                ColumnRefOperator owner = queryFactory.getColumnRef(id);
                // ColumnRefOperator.equals compares ids only, so it cannot answer this. A
                // different object carrying the same id and the same identity is fine -- clones
                // are everywhere -- so only disagreeing fields count.
                if (owner != ref && (owner.getOpType() != ref.getOpType()
                        || !owner.getName().equals(ref.getName())
                        || !owner.getType().equals(ref.getType()))) {
                    unowned.add(describe(ref) + " -> the query factory holds id " + id + " as "
                            + describe(owner));
                }
            }

            Assertions.assertFalse(lambdaArgs.isEmpty(),
                    "no lambda argument reached the physical plan, so this test proves nothing");
            Assertions.assertTrue(unowned.isEmpty(),
                    "plan contains column refs the query factory does not own, i.e. ids leaked in "
                            + "from another ColumnRefFactory:\n  " + String.join("\n  ", unowned)
                            + "\nlambda arguments seen: " + lambdaArgs);
        });
    }

    private static String describe(ColumnRefOperator ref) {
        return ref.getId() + ":" + ref.getName() + " " + ref.getType() + " [" + ref.getOpType() + "]";
    }

    private static void collectColumnRefs(OptExpression expr, List<ColumnRefOperator> out) {
        if (expr == null) {
            return;
        }
        collectFromScalar(expr.getOp().getPredicate(), out);
        Projection projection = expr.getOp().getProjection();
        if (projection != null) {
            for (Map.Entry<ColumnRefOperator, ScalarOperator> entry : projection.getColumnRefMap().entrySet()) {
                collectFromScalar(entry.getKey(), out);
                collectFromScalar(entry.getValue(), out);
            }
            for (Map.Entry<ColumnRefOperator, ScalarOperator> entry :
                    projection.getCommonSubOperatorMap().entrySet()) {
                collectFromScalar(entry.getKey(), out);
                collectFromScalar(entry.getValue(), out);
            }
        }
        for (OptExpression child : expr.getInputs()) {
            collectColumnRefs(child, out);
        }
    }

    private static void collectFromScalar(ScalarOperator scalarOperator, List<ColumnRefOperator> out) {
        if (scalarOperator == null) {
            return;
        }
        if (scalarOperator instanceof ColumnRefOperator) {
            out.add((ColumnRefOperator) scalarOperator);
        }
        if (scalarOperator instanceof LambdaFunctionOperator) {
            LambdaFunctionOperator lambda = (LambdaFunctionOperator) scalarOperator;
            // refColumns is not a child, and the hoisted columnRefMap values are only reachable through the
            // map, so neither is covered by the getChildren() walk below.
            out.addAll(lambda.getRefColumns());
            for (Map.Entry<ColumnRefOperator, ScalarOperator> entry : lambda.getColumnRefMap().entrySet()) {
                collectFromScalar(entry.getKey(), out);
                collectFromScalar(entry.getValue(), out);
            }
        }
        for (ScalarOperator child : scalarOperator.getChildren()) {
            collectFromScalar(child, out);
        }
    }
}
