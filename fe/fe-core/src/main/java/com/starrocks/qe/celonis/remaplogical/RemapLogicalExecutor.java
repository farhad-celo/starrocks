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

package com.starrocks.qe.celonis.remaplogical;

import com.starrocks.analysis.Expr;
import com.starrocks.analysis.ParseNode;
import com.starrocks.analysis.SlotRef;
import com.starrocks.analysis.TableName;
import com.starrocks.common.util.ParseUtil;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.analyzer.AstToSQLBuilder;
import com.starrocks.sql.analyzer.Field;
import com.starrocks.sql.ast.CTERelation;
import com.starrocks.sql.ast.FieldReference;
import com.starrocks.sql.ast.JoinRelation;
import com.starrocks.sql.ast.PivotAggregation;
import com.starrocks.sql.ast.PivotRelation;
import com.starrocks.sql.ast.PivotValue;
import com.starrocks.sql.ast.QueryRelation;
import com.starrocks.sql.ast.QueryStatement;
import com.starrocks.sql.ast.Relation;
import com.starrocks.sql.ast.SelectRelation;
import com.starrocks.sql.ast.SubqueryRelation;
import com.starrocks.sql.ast.TableFunctionRelation;
import com.starrocks.sql.ast.TableRelation;
import com.starrocks.sql.ast.ValuesRelation;
import com.starrocks.sql.ast.ViewRelation;
import com.starrocks.sql.ast.celonis.remaplogical.RemapLogicalStmt;
import com.starrocks.sql.ast.celonis.remaplogical.RemappingSet;
import org.apache.commons.collections4.CollectionUtils;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.stream.Collectors;

public final class RemapLogicalExecutor {
    private RemapLogicalExecutor() {
    }

    /**
     * Format analyzed REMAP LOGICAL output. The inner query must already have been analyzed.
     */
    public static String execute(RemapLogicalStmt statement, ConnectContext connectContext) {
        RemappingSet remappingSet = Objects.requireNonNull(statement.getRemappingSet(),
                "REMAP LOGICAL statement has not been analyzed");
        return new RemapLogicalSqlBuilder(remappingSet, connectContext).visit(statement.getQueryStmt());
    }

    private static final class RemapLogicalSqlBuilder extends AstToSQLBuilder.AST2SQLBuilderVisitor {
        private final RemappingSet remappingSet;
        private final ConnectContext connectContext;
        private final Deque<Map<String, RelationBinding>> relationScopes = new ArrayDeque<>();

        private RemapLogicalSqlBuilder(RemappingSet remappingSet, ConnectContext connectContext) {
            super(false, false, true);
            this.remappingSet = remappingSet;
            this.connectContext = connectContext;
        }

        @Override
        public String visitQueryStatement(QueryStatement stmt, Void context) {
            QueryRelation queryRelation = stmt.getQueryRelation();
            if (!(queryRelation instanceof SelectRelation selectRelation)) {
                return super.visitQueryStatement(stmt, context);
            }

            // Query-level clauses such as ORDER BY are rendered after visitSelect() returns, so keep the same
            // relation-alias bindings alive for the whole query statement.
            relationScopes.push(collectRelationBindings(selectRelation.getRelation()));
            try {
                return super.visitQueryStatement(stmt, context);
            } finally {
                relationScopes.pop();
            }
        }

        @Override
        public String visitSelect(SelectRelation stmt, Void context) {
            relationScopes.push(collectRelationBindings(stmt.getRelation()));
            try {
                return super.visitSelect(stmt, context);
            } finally {
                relationScopes.pop();
            }
        }

        @Override
        protected List<String> visitSelectItemList(SelectRelation stmt) {
            // The raw-select-list-item fallback (unanalyzed AST) is unreachable here: REMAP LOGICAL always fully
            // analyzes its inner query, so getOutputExpression() is populated by the time this renders.
            if (CollectionUtils.isNotEmpty(stmt.getOutputExpression())) {
                return buildSelectListFromOutputExpressions(stmt);
            }
            return super.visitSelectItemList(stmt);
        }

        private List<String> buildSelectListFromOutputExpressions(SelectRelation stmt) {
            List<String> selectListString = new ArrayList<>();
            List<String> columnNameList = stmt.getColumnOutputNames();
            for (int i = 0; i < stmt.getOutputExpression().size(); ++i) {
                Expr expr = stmt.getOutputExpression().get(i);
                String columnName = columnNameList.get(i);

                if (expr instanceof FieldReference) {
                    Field field = stmt.getScope().getRelationFields().getFieldByIndex(i);
                    selectListString.add(buildRemappedColumnName(field.getRelationAlias(), field.getName(), columnName));
                } else if (expr instanceof SlotRef slot) {
                    selectListString.add(buildRemappedColumnName(slot.getTblNameWithoutAnalyzed(), slot.getColumnName(),
                            columnName));
                } else if (columnName != null) {
                    selectListString.add(visit(expr) + " AS `" + columnName + "`");
                } else {
                    selectListString.add(visit(expr));
                }
            }
            return selectListString;
        }

        @Override
        public String visitTable(TableRelation node, Void outerScope) {
            // Render via a proxy node carrying the remapped name so AstToSQLBuilder's own visitTable renders the
            // partition/hint/sample/alias suffix -- avoids duplicating that logic here.
            TableRelation remapped = new TableRelation(remapTableName(normalizeTableName(node.getName())),
                    node.getPartitionNames(), node.getTabletIds(), node.getReplicaIds(), node.getPos());
            remapped.setSampleClause(node.getSampleClause());
            remapped.setAlias(node.getAlias());
            remapped.getTableHints().addAll(node.getTableHints());
            return super.visitTable(remapped, outerScope);
        }

        @Override
        public String visitView(ViewRelation node, Void context) {
            StringBuilder sqlBuilder = new StringBuilder();
            sqlBuilder.append(remapTableName(normalizeTableName(node.getName())).toSql());

            if (node.getAlias() != null) {
                sqlBuilder.append(" AS ");
                sqlBuilder.append("`").append(node.getAlias().getTbl()).append("`");
            }
            return sqlBuilder.toString();
        }

        @Override
        public String visitPivotRelation(PivotRelation node, Void context) {
            // Mirrors AstToStringBuilder#visitPivotRelation, but routes the aggregate expressions and pivot
            // columns through visit() (rather than toSqlImpl()/getColumnName()) so they reach this visitor's
            // visitSlot() and get remapped like any other column reference.
            StringBuilder sb = new StringBuilder();
            sb.append(visit(Objects.requireNonNull(node.getQuery())));
            sb.append(" PIVOT (");
            boolean first = true;
            for (PivotAggregation aggregation : node.getAggregateFunctions()) {
                if (!first) {
                    sb.append(", ");
                }
                first = false;
                sb.append(visit(aggregation.getFunctionCallExpr()));
                if (aggregation.getAlias() != null) {
                    sb.append(" AS ").append(aggregation.getAlias());
                }
            }
            sb.append("\n");

            sb.append("FOR ");
            sb.append(renderOneOrParenthesizedMany(node.getPivotColumns()));

            sb.append(" IN (");
            first = true;
            for (PivotValue pivotValue : node.getPivotValues()) {
                if (!first) {
                    sb.append(", ");
                }
                first = false;
                sb.append(renderOneOrParenthesizedMany(pivotValue.getExprs()));
                if (pivotValue.getAlias() != null) {
                    sb.append(" AS ").append(pivotValue.getAlias());
                }
            }
            sb.append(")\n)");

            return sb.toString();
        }

        // Renders a single node bare, or multiple nodes as a comma-joined, parenthesized list. Used for both
        // PIVOT's key-column list and each value tuple in its IN (...) list.
        private String renderOneOrParenthesizedMany(List<? extends ParseNode> nodes) {
            if (nodes.size() == 1) {
                return visit(nodes.get(0));
            }
            return nodes.stream().map(this::visit).collect(Collectors.joining(", ", "(", ")"));
        }

        @Override
        public String visitSlot(SlotRef expr, Void context) {
            // Slot rendering is also used inside expressions; SELECT-list aliases are added by visitSelectItemList().
            return buildRemappedColumnName(expr.getTblNameWithoutAnalyzed(), expr.getColumnName(), null);
        }

        private String buildRemappedColumnName(TableName qualifier, String sourceColumn, String outputColumn) {
            TableName sourceTable = sourceTableFor(qualifier);
            String remappedColumn = remapColumnPath(sourceTable, sourceColumn);
            String renderedQualifier = renderQualifier(qualifier);
            return buildScalarColumnName(renderedQualifier, remappedColumn, outputColumn);
        }

        private String buildScalarColumnName(String qualifier, String fieldName, String columnName) {
            StringBuilder result = new StringBuilder();
            if (qualifier != null && !withoutTbl) {
                result.append(qualifier).append(".");
            }
            result.append("`").append(fieldName).append("`");
            if (columnName != null && !fieldName.equalsIgnoreCase(columnName)) {
                result.append(" AS `").append(columnName).append("`");
            }
            return result.toString();
        }

        private String remapColumnPath(TableName sourceTable, String sourceColumn) {
            if (sourceTable == null || sourceColumn == null) {
                return sourceColumn;
            }
            int firstDot = sourceColumn.indexOf('.');
            if (firstDot < 0) {
                return remappingSet.remapColumnName(sourceTable, sourceColumn);
            }
            String baseColumn = sourceColumn.substring(0, firstDot);
            return remappingSet.remapColumnName(sourceTable, baseColumn) + sourceColumn.substring(firstDot);
        }

        private String renderQualifier(TableName qualifier) {
            if (qualifier == null) {
                return null;
            }
            RelationBinding binding = findBinding(qualifier);
            if (binding != null) {
                return binding.outputQualifierSql();
            }
            return remapTableName(normalizeTableName(qualifier)).toSql();
        }

        private TableName sourceTableFor(TableName qualifier) {
            if (qualifier == null) {
                return null;
            }
            RelationBinding binding = findBinding(qualifier);
            if (binding != null) {
                return binding.sourceTable();
            }
            return normalizeTableName(qualifier);
        }

        private RelationBinding findBinding(TableName qualifier) {
            String bareKey = qualifier.getTbl().toLowerCase(Locale.ROOT);
            String qualifiedKey = relationKey(qualifier);
            String normalizedQualifiedKey = (qualifier.getDb() != null || qualifier.getCatalog() != null)
                    ? relationKey(normalizeTableName(qualifier)) : null;

            // Search one lexical scope at a time. An analyzed alias can carry the source table's db/catalog, so it
            // still needs a bare-name fallback. However, for an explicitly qualified relation, that fallback must
            // not win over a full table key in the same scope when a different relation uses the table name as an
            // alias (for example, test2.dup_t alongside "test.t0 AS dup_t").
            for (Map<String, RelationBinding> scope : relationScopes) {
                RelationBinding binding = scope.get(qualifiedKey);
                if (binding != null) {
                    return binding;
                }
                if (normalizedQualifiedKey != null && !normalizedQualifiedKey.equals(qualifiedKey)) {
                    binding = scope.get(normalizedQualifiedKey);
                    if (binding != null) {
                        return binding;
                    }
                }
                binding = scope.get(bareKey);
                if (binding != null) {
                    return binding;
                }
            }
            return null;
        }

        private Map<String, RelationBinding> collectRelationBindings(Relation relation) {
            Map<String, RelationBinding> bindings = new HashMap<>();
            collectRelationBindings(relation, bindings);
            return bindings;
        }

        private void collectRelationBindings(Relation relation, Map<String, RelationBinding> bindings) {
            if (relation == null) {
                return;
            }
            if (relation instanceof JoinRelation joinRelation) {
                collectRelationBindings(joinRelation.getLeft(), bindings);
                collectRelationBindings(joinRelation.getRight(), bindings);
            } else if (relation instanceof TableRelation tableRelation) {
                bindNamedRelation(tableRelation.getName(), tableRelation.getAlias(), bindings);
            } else if (relation instanceof ViewRelation viewRelation) {
                bindNamedRelation(viewRelation.getName(), viewRelation.getAlias(), bindings);
            } else if (relation instanceof SubqueryRelation || relation instanceof ValuesRelation ||
                    relation instanceof TableFunctionRelation || relation instanceof CTERelation) {
                bindAliasOnlyRelation(relation, bindings);
            } else if (relation instanceof PivotRelation pivotRelation) {
                collectRelationBindings(pivotRelation.getQuery(), bindings);
                if (pivotRelation.getAlias() != null) {
                    bindAliasOnlyRelation(pivotRelation, bindings);
                }
            }
        }

        private void bindNamedRelation(TableName rawName, TableName alias,
                                       Map<String, RelationBinding> bindings) {
            TableName sourceTable = normalizeTableName(rawName);
            String outputQualifier = alias == null ? remapTableName(sourceTable).toSql() : renderAlias(alias);
            TableName scopeKey = alias == null ? sourceTable : alias;
            bindings.put(relationKey(scopeKey), new RelationBinding(sourceTable, outputQualifier));
        }

        private void bindAliasOnlyRelation(Relation relation, Map<String, RelationBinding> bindings) {
            TableName alias = relation.getAlias();
            if (alias == null) {
                alias = relation.getResolveTableName();
            }
            if (alias != null) {
                bindings.put(relationKey(alias), new RelationBinding(null, renderAlias(alias)));
            }
        }

        private TableName remapTableName(TableName tableName) {
            return remappingSet.remapTableName(tableName);
        }

        private TableName normalizeTableName(TableName tableName) {
            TableName normalizedTableName = new TableName(tableName.getCatalog(), tableName.getDb(), tableName.getTbl(),
                    tableName.getPos());
            normalizedTableName.normalization(connectContext);
            return normalizedTableName;
        }

        /**
         * Keys an alias by its bare name (aliases have no catalog/db), but keys an unaliased relation by its full
         * normalized catalog/db/table identity so that same-named tables from different databases (e.g. db1.orders
         * and db2.orders joined without aliases) do not collide in {@link #relationScopes}.
         */
        private static String relationKey(TableName tableName) {
            String tbl = tableName.getTbl().toLowerCase(Locale.ROOT);
            if (tableName.getDb() == null && tableName.getCatalog() == null) {
                return tbl;
            }
            String db = tableName.getDb() == null ? "" : tableName.getDb().toLowerCase(Locale.ROOT);
            String catalog = tableName.getCatalog() == null ? "" : tableName.getCatalog().toLowerCase(Locale.ROOT);
            return catalog + "." + db + "." + tbl;
        }

        private static String renderAlias(TableName alias) {
            return ParseUtil.backquote(alias.getTbl());
        }
    }

    private record RelationBinding(TableName sourceTable, String outputQualifierSql) {}
}
