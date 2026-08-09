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
import com.google.common.collect.Sets;
import com.starrocks.sql.optimizer.base.ColumnRefFactory;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.LambdaFunctionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperatorVisitor;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Replace the corresponding ColumnRef with ScalarOperator
public class ReplaceColumnRefRewriter {
    private final Rewriter rewriter = new Rewriter();
    private final Map<ColumnRefOperator, ? extends ScalarOperator> operatorMap;

    private final boolean isRecursively;

    // Non-null only for the duplicating variant, see the constructor below.
    private final ColumnRefFactory columnRefFactory;

    public ReplaceColumnRefRewriter(Map<ColumnRefOperator, ? extends ScalarOperator> operatorMap) {
        this(operatorMap, false, null);
    }

    public ReplaceColumnRefRewriter(Map<ColumnRefOperator, ? extends ScalarOperator> operatorMap,
                                    boolean isRecursively) {
        this(operatorMap, isRecursively, null);
    }

    /**
     * Duplicating variant, for callers that copy an expression into a new plan fragment rather than
     * substitute within an existing one.
     *
     * <p>Passing a factory additionally re-binds the arguments of every {@link LambdaFunctionOperator} in the
     * result to fresh ids. Plain substitution cannot do that: lambda arguments are excluded from
     * {@code getUsedColumns()}, so they never appear in {@code operatorMap}, and a lambda's only child is its
     * body, so walking children leaves the argument list alone. Copies would then share one set of argument
     * ids, which breaks once the copies are rewritten independently and their argument types diverge --
     * {@code ScalarOperatorToExpr} registers every lambda argument into a single fragment-wide
     * colRef-to-expr map keyed by id.
     *
     * <p>Use one rewriter per copy. The re-binding is remembered for the lifetime of the rewriter, so a lambda
     * reached twice while copying one fragment keeps identical argument ids in both places and the two stay
     * structurally equal, which sub-expression reuse and MV equation matching rely on; a different rewriter
     * picks different ids, which is what keeps two copies apart.
     *
     * <p>The factory must be the one owning the plan the copy belongs to; query side and MV side differ.
     */
    public ReplaceColumnRefRewriter(Map<ColumnRefOperator, ? extends ScalarOperator> operatorMap,
                                    boolean isRecursively, ColumnRefFactory columnRefFactory) {
        this.operatorMap = operatorMap;
        this.isRecursively = isRecursively;
        this.columnRefFactory = columnRefFactory;
    }

    public ScalarOperator rewrite(ScalarOperator origin) {
        if (origin == null) {
            return null;
        }

        ScalarOperator result = origin.clone().accept(rewriter, null);
        // Check expression complexity after column reference replacement
        // Use cached value (true) since clone() has already cleared the cache
        result.checkMaxFlatChildren(true);
        return result;
    }

    public ScalarOperator rewriteWithoutClone(ScalarOperator origin) {
        if (origin == null) {
            return null;
        }

        ScalarOperator result = origin.accept(rewriter, null);
        // Check expression complexity after column reference replacement
        // Force recalculation (false) since the expression structure has been modified without cloning
        result.checkMaxFlatChildren(false);
        return result;
    }

    private class Rewriter extends ScalarOperatorVisitor<ScalarOperator, Void> {
        // Track keys that are temporarily excluded from replacement during recursive rewriting
        // to prevent cycles when a replaced expression contains the original column reference
        private final Set<ColumnRefOperator> excludedKeys = Sets.newHashSet();

        // The new ref chosen for each lambda binding site, keyed by the id being replaced. Empty unless this
        // is the duplicating variant. Never cleared: it is what makes one rewriter agree with itself across
        // the many expressions of a single copy. Ids are unique per factory, so one old id belongs to exactly
        // one binding site and the mapping is unambiguous.
        private final Map<Integer, ColumnRefOperator> reboundArguments = Maps.newHashMap();

        // The subset of reboundArguments that is in scope at the current point of the walk. Entries are added
        // when a lambda is entered and removed when it is left, so a nested lambda still sees the arguments of
        // its enclosing lambdas -- x -> array_map(y -> x + y, arr) requires the inner body to resolve x to the
        // outer lambda's new ref -- while a reference outside the lambda that binds an id is left alone.
        private final Map<Integer, ColumnRefOperator> inScopeArguments = Maps.newHashMap();

        @Override
        public ScalarOperator visit(ScalarOperator scalarOperator, Void context) {
            List<ScalarOperator> children = Lists.newArrayList(scalarOperator.getChildren());
            for (int i = 0; i < children.size(); ++i) {
                scalarOperator.setChild(i, scalarOperator.getChild(i).accept(this, null));
            }
            return scalarOperator;
        }

        @Override
        public ScalarOperator visitLambdaFunctionOperator(LambdaFunctionOperator operator, Void context) {
            if (columnRefFactory == null) {
                // Pure substitution: the argument list is left exactly as it is.
                return visit(operator, context);
            }

            // Remember the binding each id had on the way in so it can be restored on the way out.
            Map<Integer, ColumnRefOperator> shadowed = Maps.newHashMap();
            try {
                // The argument list is positional: canReduce() indexes into it and ScalarOperatorToExpr
                // appends the arguments after the body in this order, so rebuild it index by index.
                List<ColumnRefOperator> newRefColumns =
                        Lists.newArrayListWithCapacity(operator.getRefColumns().size());
                for (ColumnRefOperator refColumn : operator.getRefColumns()) {
                    newRefColumns.add(rebind(shadowed, refColumn, true));
                }

                // Common sub-expressions hoisted out of the body are lambda-local too, and the body refers to
                // their keys, so they have to be handled before the body. The map is ordered by id and a later
                // entry may reference an earlier one, which iterating in that order also takes care of.
                Map<ColumnRefOperator, ScalarOperator> newColumnRefMap = Map.of();
                if (!operator.getColumnRefMap().isEmpty()) {
                    newColumnRefMap = new LinkedHashMap<>();
                    for (Map.Entry<ColumnRefOperator, ScalarOperator> entry : operator.getColumnRefMap().entrySet()) {
                        ScalarOperator newValue = entry.getValue().accept(this, null);
                        // These keys stand for hoisted sub-expressions, not for bound arguments, so they are
                        // plain refs rather than lambda arguments.
                        newColumnRefMap.put(rebind(shadowed, entry.getKey(), false), newValue);
                    }
                }

                LambdaFunctionOperator newOperator = new LambdaFunctionOperator(newRefColumns,
                        operator.getLambdaExpr().accept(this, null), operator.getType());
                if (!newColumnRefMap.isEmpty()) {
                    newOperator.addColumnToExpr(newColumnRefMap);
                }
                return newOperator;
            } finally {
                shadowed.forEach((id, previous) -> {
                    if (previous == null) {
                        inScopeArguments.remove(id);
                    } else {
                        inScopeArguments.put(id, previous);
                    }
                });
            }
        }

        /**
         * Allocates (or recalls) the replacement for one lambda-local ref and brings it into scope.
         * isLambdaArgument must be preserved for bound arguments: a plain VARIABLE ref would start reporting
         * itself as a used column, making the argument look like a dependency on an outer scope.
         */
        private ColumnRefOperator rebind(Map<Integer, ColumnRefOperator> shadowed, ColumnRefOperator ref,
                                         boolean isLambdaArgument) {
            ColumnRefOperator replacement = reboundArguments.computeIfAbsent(ref.getId(),
                    id -> columnRefFactory.create(ref.getName(), ref.getType(), ref.isNullable(), isLambdaArgument));
            ColumnRefOperator previous = inScopeArguments.put(ref.getId(), replacement);
            // Not putIfAbsent: previous may legitimately be null, which that method cannot distinguish
            // from an absent key.
            if (!shadowed.containsKey(ref.getId())) {
                shadowed.put(ref.getId(), previous);
            }
            return replacement;
        }

        @Override
        public ScalarOperator visitVariableReference(ColumnRefOperator column, Void context) {
            // A lambda-local ref: replaced by its re-binding, and never by operatorMap, which only ever
            // carries refs visible to outer scopes.
            ColumnRefOperator rebound = inScopeArguments.get(column.getId());
            if (rebound != null) {
                return rebound;
            }
            // If this column is excluded, don't replace it (prevents cycles)
            if (excludedKeys.contains(column)) {
                return column;
            }
            if (!operatorMap.containsKey(column)) {
                return column;
            }
            // Must clone here because
            // The rewritten predicate will be rewritten continually,
            // Rewiring predicate shouldn't change the origin project columnRefMap

            ScalarOperator mapperOperator = operatorMap.get(column);
            if (column.equals(mapperOperator)) {
                return column;
            }
            if (!isRecursively) {
                // Not descended into, so a lambda inside the substituted expression keeps its argument ids.
                // Duplicating callers map to plain column refs, so this does not arise for them.
                return mapperOperator.clone();
            } else {
                while (mapperOperator instanceof ColumnRefOperator && operatorMap.containsKey(mapperOperator)) {
                    ScalarOperator mapped = operatorMap.get(mapperOperator);
                    if (mapped.equals(mapperOperator)) {
                        break;
                    }
                    mapperOperator = mapped;
                }
                mapperOperator = mapperOperator.clone();

                // Temporarily exclude this key from replacement when recursively rewriting children
                // to prevent cycles when the replacement contains the original column reference
                excludedKeys.add(column);
                try {
                    for (int i = 0; i < mapperOperator.getChildren().size(); ++i) {
                        mapperOperator.setChild(i, mapperOperator.getChild(i).accept(this, null));
                    }
                } finally {
                    // Restore the key after rewriting children
                    excludedKeys.remove(column);
                }
            }
            return mapperOperator;
        }
    }
}
