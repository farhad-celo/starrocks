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

package com.starrocks.sql.optimizer.rule.tree;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.OptExpressionVisitor;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.Projection;
import com.starrocks.sql.optimizer.operator.physical.PhysicalJoinOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalProjectOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperatorVisitor;
import com.starrocks.sql.optimizer.task.TaskContext;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * Ensures no two physical operators use the same lambda argument id.
 *
 * <p>Ids are keyed on (operator, argument id) and resolved one argument at a time. The first operator to use
 * an id keeps it, so plans holding no duplicate are left untouched; any other operator arriving with the same
 * id gets a fresh one.
 *
 * <p>Occurrences within a single operator are deliberately left alone, sharing one id as they arrived.
 * ScalarOperatorsReuse needs that to recognise them as one expression and evaluate the call once.
 *
 * <p>Visits projections, predicates and join on-predicates. Aggregate calls ({@code getAggregations}) and
 * window calls ({@code getAnalyticCall}) hold their expressions in final fields and would need an operator
 * rebuild, so they are not covered.
 *
 * <p>Invoked as the first step of {@code LowCardinalityRewriteRule}, so the dictionary rewrite that follows it
 * sees settled ids. That is also ahead of ScalarOperatorsReuseRule, where a lambda's hoisted
 * {@code columnRefMap} is still empty.
 */
public class UniqueLambdaArgumentRule implements TreeRewriteRule {
    @Override
    public OptExpression rewrite(OptExpression root, TaskContext taskContext) {
        ColumnRefFactory columnRefFactory = taskContext.getOptimizerContext().getColumnRefFactory();
        new Visitor(columnRefFactory).visit(root, null);
        return root;
    }

    /**
     * One operator plus the id an argument arrived with. Compares {@code owner} by identity, since
     * {@link Operator#equals} is structural and two structurally equal operators are still two operators;
     * {@code hashCode} likewise, and because {@code Operator.hashCode} folds in {@code predicate} and
     * {@code projection}, which this rule mutates in place.
     */
    private static final class BindingKey {
        private final Operator owner;
        private final int argumentId;

        private BindingKey(Operator owner, int argumentId) {
            this.owner = owner;
            this.argumentId = argumentId;
        }

        @Override
        public boolean equals(Object other) {
            if (!(other instanceof BindingKey)) {
                return false;
            }
            BindingKey that = (BindingKey) other;
            // Operator identity, not equality: two structurally equal operators are still two operators.
            return this.owner == that.owner && this.argumentId == that.argumentId;
        }

        @Override
        public int hashCode() {
            return Objects.hash(System.identityHashCode(owner), argumentId);
        }
    }

    private static class Visitor extends OptExpressionVisitor<Void, Void> {
        private final ColumnRefFactory columnRefFactory;

        /** Argument ids already spoken for by some binding. */
        private final Set<Integer> claimedIds = Sets.newHashSet();

        /** The ref handed out per binding, so a binding reached twice resolves the same way. */
        private final Map<BindingKey, ColumnRefOperator> assigned = Maps.newHashMap();

        Visitor(ColumnRefFactory columnRefFactory) {
            this.columnRefFactory = columnRefFactory;
        }

        @Override
        public Void visit(OptExpression optExpression, Void context) {
            Operator op = optExpression.getOp();
            LambdaRewriter rewriter = new LambdaRewriter(op);

            if (op instanceof PhysicalProjectOperator) {
                PhysicalProjectOperator project = op.cast();
                rewriteInPlace(project.getColumnRefMap(), rewriter);
                rewriteInPlace(project.getCommonSubOperatorMap(), rewriter);
            }
            Projection projection = op.getProjection();
            if (projection != null) {
                rewriteInPlace(projection.getColumnRefMap(), rewriter);
                rewriteInPlace(projection.getCommonSubOperatorMap(), rewriter);
            }
            if (op.getPredicate() != null) {
                op.setPredicate(rewriter.rewrite(op.getPredicate()));
            }
            if (op instanceof PhysicalJoinOperator) {
                PhysicalJoinOperator join = op.cast();
                if (join.getOnPredicate() != null) {
                    join.setOnPredicate(rewriter.rewrite(join.getOnPredicate()));
                }
            }

            optExpression.getInputs().forEach(input -> this.visit(input, context));
            return null;
        }

        private void rewriteInPlace(Map<ColumnRefOperator, ScalarOperator> map, LambdaRewriter rewriter) {
            if (map == null || map.isEmpty()) {
                return;
            }
            for (ColumnRefOperator key : Lists.newArrayList(map.keySet())) {
                map.put(key, rewriter.rewrite(map.get(key)));
            }
        }

        /** The ref this argument should use, allocated the first time the binding is seen. */
        private ColumnRefOperator resolve(BindingKey key, ColumnRefOperator original) {
            ColumnRefOperator existing = assigned.get(key);
            if (existing != null) {
                return existing;
            }

            // Past the early return, so any claim on the original id belongs to a different binding.
            ColumnRefOperator result;
            if (!claimedIds.contains(original.getId())) {
                result = original;
            } else {
                // isLambdaArgument must be preserved: a plain VARIABLE ref would start reporting itself as a
                // used column, making the argument look like an outer-scope dependency.
                result = columnRefFactory.create(
                        original.getName(), original.getType(), original.isNullable(), true);
            }
            claimedIds.add(result.getId());
            assigned.put(key, result);
            return result;
        }

        /** One instance per operator, so {@code owner} is fixed for the whole walk. */
        private class LambdaRewriter extends ScalarOperatorVisitor<ScalarOperator, Void> {
            private final Operator owner;

            /**
             * Argument substitutions in effect here, pushed on entering a lambda and popped on leaving it.
             * Scoped rather than per-operator for two reasons: a nested body must resolve an enclosing
             * lambda's argument, and an argument id may collide with a real column in the same operator --
             * given {@code {10: a = array_map(x[8] -> …, arr), 11: b = 8: mapped}}, a binding for 8 that
             * outlived {@code a} would rewrite {@code b}'s reference to the actual column 8.
             */
            private final Map<Integer, ColumnRefOperator> inScope = Maps.newHashMap();

            LambdaRewriter(Operator owner) {
                this.owner = owner;
            }

            ScalarOperator rewrite(ScalarOperator expression) {
                return expression == null ? null : expression.accept(this, null);
            }

            @Override
            public ScalarOperator visit(ScalarOperator expression, Void context) {
                for (int i = 0; i < expression.getChildren().size(); i++) {
                    expression.setChild(i, expression.getChild(i).accept(this, null));
                }
                return expression;
            }

            @Override
            public ScalarOperator visitVariableReference(ColumnRefOperator variable, Void context) {
                ColumnRefOperator rebound = inScope.get(variable.getId());
                return rebound == null ? variable : rebound;
            }

            @Override
            public ScalarOperator visitLambdaFunctionOperator(LambdaFunctionOperator lambda, Void context) {
                List<ColumnRefOperator> original = lambda.getRefColumns();
                // Positional: canReduce() indexes into refColumns and ScalarOperatorToExpr appends the
                // arguments after the body in this order, so rebuild it index by index.
                List<ColumnRefOperator> resolved = Lists.newArrayListWithCapacity(original.size());
                boolean unchanged = true;
                for (ColumnRefOperator ref : original) {
                    ColumnRefOperator target = resolve(new BindingKey(owner, ref.getId()), ref);
                    unchanged &= target == ref;
                    resolved.add(target);
                }

                // Values may be null, meaning the id was unbound before.
                Map<Integer, ColumnRefOperator> shadowed = Maps.newHashMapWithExpectedSize(original.size());
                try {
                    for (int i = 0; i < original.size(); i++) {
                        shadowed.put(original.get(i).getId(),
                                inScope.put(original.get(i).getId(), resolved.get(i)));
                    }
                    ScalarOperator newBody = lambda.getLambdaExpr().accept(this, null);
                    if (unchanged && newBody == lambda.getLambdaExpr()) {
                        return lambda;
                    }
                    LambdaFunctionOperator rewritten =
                            new LambdaFunctionOperator(resolved, newBody, lambda.getType());
                    if (!lambda.getColumnRefMap().isEmpty()) {
                        // Empty where this rule runs; carried over rather than dropped if it moves later.
                        rewritten.addColumnToExpr(lambda.getColumnRefMap());
                    }
                    return rewritten;
                } finally {
                    shadowed.forEach((id, previous) -> {
                        if (previous == null) {
                            inScope.remove(id);
                        } else {
                            inScope.put(id, previous);
                        }
                    });
                }
            }
        }
    }
}
