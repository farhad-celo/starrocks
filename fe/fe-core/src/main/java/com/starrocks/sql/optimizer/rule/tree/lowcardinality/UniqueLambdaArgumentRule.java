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

package com.starrocks.sql.optimizer.rule.tree.lowcardinality;

import com.google.common.base.Preconditions;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.OptExpressionVisitor;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.Projection;
import com.starrocks.sql.optimizer.operator.physical.PhysicalHashAggregateOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalJoinOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalProjectOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalTopNOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalWindowOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperatorVisitor;
import com.starrocks.sql.optimizer.rule.tree.TreeRewriteRule;
import com.starrocks.sql.optimizer.task.TaskContext;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * Ensures no two physical operators use the same lambda argument id.
 *
 * <p>Ids are keyed on (operator, argument id), the operator compared by identity since two structurally equal
 * operators are still two operators. The first operator to use an id keeps it, so plans holding no duplicate are
 * left untouched; any other operator arriving with the same id gets a fresh one, allocated with
 * {@code isLambdaArgument} set so the argument does not start reporting itself as a used column.
 *
 * <p>Occurrences within a single operator are deliberately left alone, sharing one id as they arrived.
 * ScalarOperatorsReuse needs that to recognise them as one expression and evaluate the call once. Substitutions
 * are therefore scoped to the lambda body introducing them: a nested body must resolve an enclosing lambda's
 * argument, and an argument id may collide with a real column elsewhere in the same operator.
 *
 * <p>Visits every scalar-bearing field of the operators the low-cardinality framework can dictify: projections,
 * predicates, join on-predicates, aggregate calls, window calls and TopN pre-aggregation calls. Expression trees
 * are mutated in place rather than written back, the maps holding them being caller-owned and not necessarily
 * mutable -- {@code CTEProduceAddProjectionRule} builds a Repeat projection from {@code ImmutableMap.Builder} and
 * {@code RepeatImplementationRule} hands that same projection to the physical operator.
 *
 * <p>Operators outside that set are deliberately left alone -- values and split-consume among them. They have no
 * visitor in {@code DecodeCollector}, so they fall to its generic {@code visit}, which forces a decode; dict
 * columns never reach their fields, and an id duplicated there never reaches the dictionary rewrite. Decode
 * operators cannot be present at all, this rule running ahead of the {@code DecodeRewriter} that creates them.
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

    private record BindingKey(Operator owner, int argumentId) {

        @Override
        public boolean equals(Object other) {
            if (!(other instanceof BindingKey that)) {
                return false;
            }
            return this.owner == that.owner && this.argumentId == that.argumentId;
        }

        @Override
        public int hashCode() {
            return Objects.hash(System.identityHashCode(owner), argumentId);
        }
    }

    private static class Visitor extends OptExpressionVisitor<Void, Void> {
        private final ColumnRefFactory columnRefFactory;

        private final Set<Integer> claimedIds = Sets.newHashSet();

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
            if (op instanceof PhysicalJoinOperator join) {
                rewriteInPlace(join.getOnPredicate(), rewriter);
            }
            if (op instanceof PhysicalHashAggregateOperator aggregate) {
                aggregate.getAggregations().values().forEach(call -> rewriteInPlace(call, rewriter));
            }
            if (op instanceof PhysicalWindowOperator window) {
                window.getAnalyticCall().values().forEach(call -> rewriteInPlace(call, rewriter));
            }
            if (op instanceof PhysicalTopNOperator topN && topN.getPreAggCall() != null) {
                topN.getPreAggCall().values().forEach(call -> rewriteInPlace(call, rewriter));
            }

            optExpression.getInputs().forEach(input -> this.visit(input, context));
            return null;
        }

        private void rewriteInPlace(ScalarOperator expression, LambdaRewriter rewriter) {
            if (expression == null) {
                return;
            }
            ScalarOperator rewritten = rewriter.rewrite(expression);
            Preconditions.checkState(rewritten == expression,
                    "lambda argument rewrite replaced a root it cannot re-key: %s", expression);
        }

        private void rewriteInPlace(Map<ColumnRefOperator, ScalarOperator> map, LambdaRewriter rewriter) {
            if (map == null) {
                return;
            }
            map.values().forEach(value -> rewriteInPlace(value, rewriter));
        }

        private ColumnRefOperator resolve(BindingKey key, ColumnRefOperator original) {
            ColumnRefOperator existing = assigned.get(key);
            if (existing != null) {
                return existing;
            }

            ColumnRefOperator result;
            if (!claimedIds.contains(original.getId())) {
                result = original;
            } else {
                result = columnRefFactory.create(
                        original.getName(), original.getType(), original.isNullable(), true);
            }
            claimedIds.add(result.getId());
            assigned.put(key, result);
            return result;
        }

        private class LambdaRewriter extends ScalarOperatorVisitor<ScalarOperator, Void> {
            private final Operator owner;

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
                if (!OperatorType.LAMBDA_ARGUMENT.equals(variable.getOpType())) {
                    return variable;
                }
                ColumnRefOperator rebound = inScope.get(variable.getId());
                return rebound == null ? variable : rebound;
            }

            @Override
            public ScalarOperator visitLambdaFunctionOperator(LambdaFunctionOperator lambda, Void context) {
                List<ColumnRefOperator> original = lambda.getRefColumns();
                List<ColumnRefOperator> resolved = Lists.newArrayListWithCapacity(original.size());
                boolean unchanged = true;
                for (ColumnRefOperator ref : original) {
                    ColumnRefOperator target = resolve(new BindingKey(owner, ref.getId()), ref);
                    unchanged &= target == ref;
                    resolved.add(target);
                }

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
