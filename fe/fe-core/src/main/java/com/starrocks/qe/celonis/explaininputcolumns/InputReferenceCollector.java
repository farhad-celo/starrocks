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

package com.starrocks.qe.celonis.explaininputcolumns;

import com.starrocks.analysis.Expr;
import com.starrocks.analysis.ParseNode;
import com.starrocks.analysis.SlotRef;
import com.starrocks.analysis.TableName;
import com.starrocks.sql.analyzer.Field;
import com.starrocks.sql.analyzer.RelationFields;
import com.starrocks.sql.analyzer.celonis.CelostarAstTraverser;
import com.starrocks.sql.ast.CTERelation;
import com.starrocks.sql.ast.FileTableFunctionRelation;
import com.starrocks.sql.ast.JoinRelation;
import com.starrocks.sql.ast.NormalizedTableFunctionRelation;
import com.starrocks.sql.ast.PivotRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.Relation;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.SetOperationRelation;
import com.starrocks.sql.ast.SubqueryRelation;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.TableRelation;
import com.starrocks.sql.ast.ViewRelation;

import java.util.ArrayDeque;
import java.util.Collections;
import java.util.Deque;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Collects the physical tables/views and columns referenced by one analyzed SELECT statement.
 *
 * The existing StarRocks helpers are not sufficient for this command: {@code AstTraverser} only provides
 * structural traversal, while {@code AnalyzerUtils.collectAllSelectTableColumns} records columns but cannot retain
 * a table for references such as {@code COUNT(*)}. Analyzed {@link SlotRef}s also do not carry a stable physical
 * table identity. This collector therefore reuses the shared traversal while staging the analyzer's relation
 * scopes, then maps the resolved {@link Field} identity back to a physical table or stored view.
 *
 * Inline CTE definitions are part of the submitted SELECT and are traversed once at their declaration. Derived
 * queries are likewise traversed, whereas stored-view definitions remain opaque because they are not written
 * inline in the submitted statement.
 */
final class InputReferenceCollector extends CelostarAstTraverser {
    private final Map<TableName, Set<String>> tableColumns = new HashMap<>();
    private final Deque<RelationBindingScope> relationScopes = new ArrayDeque<>();
    private final Map<Relation, RelationBindingScope> relationScopesByIdentity = new IdentityHashMap<>();
    private final Map<Relation, PhysicalOwnerIndex> physicalOwnerIndexesByIdentity = new IdentityHashMap<>();
    private final RelationBindingScope emptyRelationScope = new RelationBindingScope(null);
    private final Set<Relation> deferredSelectRelations =
            Collections.newSetFromMap(new IdentityHashMap<>());
    private final Set<CTERelation> visitedCteDeclarations =
            Collections.newSetFromMap(new IdentityHashMap<>());

    static Map<TableName, Set<String>> collect(QueryStatement statement) {
        InputReferenceCollector collector = new InputReferenceCollector();
        collector.visit(statement);
        return collector.tableColumns;
    }

    private InputReferenceCollector() {
    }

    @Override
    public Void visit(ParseNode node, Void context) {
        if (node == null || deferredSelectRelations.contains(node)) {
            return null;
        }
        return node.accept(this, context);
    }

    @Override
    public Void visitSelect(SelectRelation node, Void context) {
        // WITH definitions are analyzed before this SELECT's FROM aliases enter scope.
        node.getCteRelations().forEach(cte -> visit(cte, context));

        // Completed SELECT expressions see the complete output scope of the FROM relation.
        relationScopes.push(relationScope(node.getRelation()));
        deferredSelectRelations.add(node.getRelation());
        try {
            // Reuse the shared traversal, deferring only the FROM tree to its staged pass below.
            super.visitSelect(node, context);
        } finally {
            deferredSelectRelations.remove(node.getRelation());
            relationScopes.pop();
        }

        // Relation-local expressions use staged scopes and are traversed separately below.
        return visit(node.getRelation(), context);
    }

    @Override
    protected void visitSetOperationOrderBy(SetOperationRelation node, Void context) {
        // Set-operation ORDER BY expressions resolve against the completed set output.
        relationScopes.push(relationScope(node));
        try {
            super.visitSetOperationOrderBy(node, context);
        } finally {
            relationScopes.pop();
        }
    }

    @Override
    public Void visitTable(TableRelation node, Void context) {
        putTableReference(node.getName(), node.isCreateByPolicyRewritten());
        return super.visitTable(node, context);
    }

    @Override
    public Void visitView(ViewRelation node, Void context) {
        putTableReference(node.getName(), node.isCreateByPolicyRewritten());
        // A stored view is a leaf because only references written in the current SELECT are reported.
        return null;
    }

    /**
     * Skips expressions synthesized by a security policy while retaining its original table or view.
     */
    @Override
    public Void visitSubqueryRelation(SubqueryRelation node, Void context) {
        if (isPolicySubquery(node)) {
            // Policy rewrites project every source column. Visit only the original relation so those synthesized
            // projections and policy expressions do not become user-visible input references.
            Relation source = policySource(node);
            return source == null ? super.visitSubqueryRelation(node, context) : visit(source, context);
        }
        return super.visitSubqueryRelation(node, context);
    }

    @Override
    public Void visitJoin(JoinRelation node, Void context) {
        visit(node.getLeft(), context);

        RelationBindingScope leftScope = relationScope(node.getLeft());
        if (node.getRight() instanceof TableFunctionRelation || node.isLateral()) {
            // The analyzer exposes the left operand only to a lateral table-function operand.
            relationScopes.push(leftScope);
            try {
                visit(node.getRight(), context);
            } finally {
                relationScopes.pop();
            }
        } else {
            // Ordinary right operands are analyzed against the parent scope, without sibling visibility.
            visit(node.getRight(), context);
        }

        // JOIN predicates and hints see both completed operand scopes plus the parent scope.
        relationScopes.push(relationScope(node.getRight()));
        relationScopes.push(leftScope);
        try {
            if (node.getOnPredicate() != null) {
                visit(node.getOnPredicate(), context);
            }
            visitJoinHints(node, context);
        } finally {
            relationScopes.pop();
            relationScopes.pop();
        }
        return null;
    }

    /**
     * Treat a normalized table function like the join relation it represents.
     */
    @Override
    public Void visitNormalizedTableFunction(NormalizedTableFunctionRelation node, Void context) {
        return visitJoin(node, context);
    }

    @Override
    public Void visitCTE(CTERelation node, Void context) {
        // The inline declaration is visited above. FROM consumers point back to that same query tree, so expanding
        // them again would duplicate traversal and would use the consumer scope instead of the declaration scope.
        if (node.isResolvedInFromClause() || !visitedCteDeclarations.add(node)) {
            return null;
        }
        return super.visitCTE(node, context);
    }

    @Override
    public Void visitPivotRelation(PivotRelation node, Void context) {
        if (node.getQuery() != null) {
            visit(node.getQuery(), context);
        }

        // PIVOT expressions are analyzed against the completed output scope of their source relation.
        relationScopes.push(relationScope(node.getQuery()));
        try {
            // Original expressions retain every aggregate argument; rewritten aggregates keep only the first.
            visitPivotExpressions(node, context);
        } finally {
            relationScopes.pop();
        }
        return null;
    }

    @Override
    public Void visitSlot(SlotRef slotRef, Void context) {
        if (!slotRef.isFromLambda() && slotRef.getTblNameWithoutAnalyzed() != null) {
            InputColumn inputColumn = resolveInputColumn(slotRef);
            if (inputColumn != null) {
                tableColumns.computeIfAbsent(inputColumn.tableName, ignored -> newColumnSet())
                        .add(inputColumn.columnName);
            }
        }
        return null;
    }

    private void putTableReference(TableName tableName, boolean createByPolicyRewritten) {
        if (!createByPolicyRewritten) {
            // An empty set is retained for references such as SELECT COUNT(*) FROM table.
            tableColumns.computeIfAbsent(tableName, ignored -> newColumnSet());
        }
    }

    private Set<String> newColumnSet() {
        return new TreeSet<>(String.CASE_INSENSITIVE_ORDER);
    }

    private RelationBindingScope relationScope(Relation relation) {
        if (relation == null) {
            return emptyRelationScope;
        }

        RelationBindingScope cachedScope = relationScopesByIdentity.get(relation);
        if (cachedScope != null) {
            return cachedScope;
        }

        RelationBindingScope scope = new RelationBindingScope(relation);
        relationScopesByIdentity.put(relation, scope);
        return scope;
    }

    private PhysicalOwnerIndex physicalOwnerIndexFor(Relation relation) {
        PhysicalOwnerIndex cachedIndex = physicalOwnerIndexesByIdentity.get(relation);
        if (cachedIndex != null) {
            return cachedIndex;
        }

        PhysicalOwnerIndex index = new PhysicalOwnerIndex(relation);
        registerPhysicalOwnerIndex(relation, index);
        return index;
    }

    private void registerPhysicalOwnerIndex(Relation relation, PhysicalOwnerIndex index) {
        if (relation == null || physicalOwnerIndexesByIdentity.containsKey(relation)) {
            return;
        }

        physicalOwnerIndexesByIdentity.put(relation, index);
        // JOIN and PIVOT preserve physical origin identity; other relation types are reporting barriers.
        if (relation instanceof PivotRelation) {
            registerPhysicalOwnerIndex(((PivotRelation) relation).getQuery(), index);
        } else if (relation instanceof SubqueryRelation && isPolicySubquery((SubqueryRelation) relation)) {
            registerPhysicalOwnerIndex(policySource((SubqueryRelation) relation), index);
        } else if (relation instanceof JoinRelation) {
            JoinRelation joinRelation = (JoinRelation) relation;
            registerPhysicalOwnerIndex(joinRelation.getLeft(), index);
            registerPhysicalOwnerIndex(joinRelation.getRight(), index);
        }
    }

    private InputColumn resolveInputColumn(SlotRef slotRef) {
        // Search inner-to-outer, but continue when the local relation has the alias without the requested field.
        for (RelationBindingScope scope : relationScopes) {
            Field field = scope.resolveField(slotRef);
            if (field != null) {
                TableName tableName = scope.resolveTableName(field);
                // A matching nonphysical or policy-generated field is a field-level reporting barrier.
                return tableName == null ? null : new InputColumn(tableName, field.getName());
            }
        }
        return null;
    }

    private static final class InputColumn {
        private final TableName tableName;
        private final String columnName;

        private InputColumn(TableName tableName, String columnName) {
            this.tableName = tableName;
            this.columnName = columnName;
        }
    }

    /**
     * Pairs analyzed fields with the physical-owner index shared by their transparent relation component.
     */
    private final class RelationBindingScope {
        private final RelationFields relationFields;
        private final PhysicalOwnerIndex physicalOwnerIndex;

        private RelationBindingScope(Relation relation) {
            this.relationFields = relation == null ? new RelationFields() : relation.getRelationFields();
            this.physicalOwnerIndex = relation == null ? null : physicalOwnerIndexFor(relation);
        }

        private Field resolveField(SlotRef slotRef) {
            // The statement is already analyzed, so a local match cannot still be ambiguous.
            List<Field> fields = relationFields.resolveFields(slotRef);
            return fields.isEmpty() ? null : fields.get(0);
        }

        private TableName resolveTableName(Field field) {
            return physicalOwnerIndex == null || field.getRelationAlias() == null
                    ? null
                    : physicalOwnerIndex.resolveTableName(field);
        }
    }

    /**
     * Indexes each transparent JOIN/PIVOT component once, on its first actual column lookup.
     *
     * Derived relations, CTEs, VALUES, table functions, and policy-generated relations are deliberately not
     * traversed here. Their output fields remain reporting barriers even when their origin expressions point at
     * physical fields inside their definitions.
     */
    private static final class PhysicalOwnerIndex {
        private final Relation root;
        private final Map<Expr, TableName> tablesByOriginExpression = new IdentityHashMap<>();
        private final Map<TableName, Map<String, TableName>> originlessTables = new HashMap<>();
        private boolean initialized;

        private PhysicalOwnerIndex(Relation root) {
            this.root = root;
        }

        private TableName resolveTableName(Field field) {
            initialize();

            if (field.getOriginExpression() != null) {
                return tablesByOriginExpression.get(field.getOriginExpression());
            }
            Map<String, TableName> columns = originlessTables.get(field.getRelationAlias());
            return columns == null ? null : columns.get(field.getName());
        }

        private void initialize() {
            if (initialized) {
                return;
            }
            initialized = true;
            indexPhysicalOwners(root);
        }

        private void indexPhysicalOwners(Relation relation) {
            if (relation instanceof FileTableFunctionRelation) {
                return;
            }
            if (relation instanceof TableRelation && !relation.isCreateByPolicyRewritten()) {
                indexPhysicalRelation(relation, ((TableRelation) relation).getName());
                return;
            }
            if (relation instanceof ViewRelation && !relation.isCreateByPolicyRewritten()) {
                indexPhysicalRelation(relation, ((ViewRelation) relation).getName());
                return;
            }
            if (relation instanceof SubqueryRelation && isPolicySubquery((SubqueryRelation) relation)) {
                SubqueryRelation policySubquery = (SubqueryRelation) relation;
                Relation source = policySource(policySubquery);
                indexPhysicalOwners(source);
                indexPolicyProjection(policySubquery, source);
            } else if (relation instanceof PivotRelation) {
                indexPhysicalOwners(((PivotRelation) relation).getQuery());
            } else if (relation instanceof JoinRelation) {
                JoinRelation joinRelation = (JoinRelation) relation;
                indexPhysicalOwners(joinRelation.getLeft());
                indexPhysicalOwners(joinRelation.getRight());
            }
        }

        /**
         * Maps fields exposed by a generated policy wrapper back to its physical source.
         */
        private void indexPolicyProjection(SubqueryRelation policySubquery, Relation source) {
            TableName physicalTableName;
            if (source instanceof TableRelation && !source.isCreateByPolicyRewritten()) {
                physicalTableName = ((TableRelation) source).getName();
            } else if (source instanceof ViewRelation && !source.isCreateByPolicyRewritten()) {
                physicalTableName = ((ViewRelation) source).getName();
            } else {
                return;
            }

            // Subquery output fields retain each synthesized projection expression as their origin. Mapping those
            // origins makes the generated wrapper transparent to explicit columns in the user's outer SELECT.
            for (Field field : policySubquery.getRelationFields().getAllFields()) {
                if (field.getOriginExpression() != null) {
                    tablesByOriginExpression.put(field.getOriginExpression(), physicalTableName);
                }
            }
        }

        private void indexPhysicalRelation(Relation relation, TableName physicalTableName) {
            for (Field field : relation.getRelationFields().getAllFields()) {
                Expr originExpression = field.getOriginExpression();
                if (originExpression != null) {
                    tablesByOriginExpression.put(originExpression, physicalTableName);
                } else if (field.getRelationAlias() != null) {
                    originlessTables.computeIfAbsent(
                                    field.getRelationAlias(),
                                    ignored -> new TreeMap<>(String.CASE_INSENSITIVE_ORDER))
                            .put(field.getName(), physicalTableName);
                }
            }
        }
    }

    /**
     * Returns the original table or view wrapped by a generated policy subquery.
     */
    private static Relation policySource(SubqueryRelation policySubquery) {
        Relation queryRelation = policySubquery.getQueryStatement().getQueryRelation();
        return queryRelation instanceof SelectRelation ? ((SelectRelation) queryRelation).getRelation() : null;
    }

    /**
     * Identifies subqueries synthesized by the security-policy rewrite.
     */
    private static boolean isPolicySubquery(SubqueryRelation subquery) {
        return subquery.getQueryStatement().getQueryRelation().isCreateByPolicyRewritten();
    }
}
