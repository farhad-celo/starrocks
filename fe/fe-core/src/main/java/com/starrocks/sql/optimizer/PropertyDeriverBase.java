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


package com.starrocks.sql.optimizer;

import com.google.common.base.Preconditions;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.catalog.ColocateTableIndex;
import com.starrocks.qe.SessionVariable;
import com.starrocks.server.GlobalStateMgr;
import com.starrocks.sql.optimizer.base.DistributionCol;
import com.starrocks.sql.optimizer.base.DistributionProperty;
import com.starrocks.sql.optimizer.base.DistributionSpec;
import com.starrocks.sql.optimizer.base.HashDistributionDesc;
import com.starrocks.sql.optimizer.base.HashDistributionSpec;
import com.starrocks.sql.optimizer.base.PhysicalPropertySet;
import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.OperatorVisitor;
import com.starrocks.sql.optimizer.operator.Projection;
import com.starrocks.sql.optimizer.operator.logical.LogicalFilterOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalOlapScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalProjectOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalSetOperation;
import com.starrocks.sql.optimizer.operator.physical.PhysicalUnionOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import org.apache.commons.collections4.CollectionUtils;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

import static com.starrocks.sql.optimizer.base.HashDistributionDesc.SourceType.SHUFFLE_JOIN;

public abstract class PropertyDeriverBase<R, C> extends OperatorVisitor<R, C> {

    public abstract R visitOperator(Operator node, C context);


    // Compute the required properties of shuffle join for children, adjust shuffle columns orders for
    // respect the required properties from parent.
    protected static List<PhysicalPropertySet> computeShuffleJoinRequiredProperties(
            PhysicalPropertySet requiredFromParent, List<DistributionCol> leftShuffleColumns,
            List<DistributionCol> rightShuffleColumns) {
        Optional<HashDistributionDesc> requiredShuffleDescOptional =
                getShuffleJoinHashDistributionDesc(requiredFromParent);
        if (!requiredShuffleDescOptional.isPresent()) {
            // required property is not SHUFFLE_JOIN
            return createShuffleJoinRequiredProperties(leftShuffleColumns, rightShuffleColumns);
        } else {
            // required property type is SHUFFLE_JOIN, adjust the required property shuffle columns based on the column
            // order required by parent
            List<DistributionCol> requiredColumns = requiredShuffleDescOptional.get().getDistributionCols();
            boolean adjustBasedOnLeft = CollectionUtils.isEqualCollection(requiredColumns, leftShuffleColumns);
            boolean adjustBasedOnRight = CollectionUtils.isEqualCollection(requiredColumns, rightShuffleColumns);

            if (adjustBasedOnLeft || adjustBasedOnRight) {
                List<DistributionCol> requiredLeft = Lists.newArrayList();
                List<DistributionCol> requiredRight = Lists.newArrayList();

                for (DistributionCol cid : requiredColumns) {
                    int idx = adjustBasedOnLeft ? leftShuffleColumns.indexOf(cid) : rightShuffleColumns.indexOf(cid);
                    requiredLeft.add(leftShuffleColumns.get(idx));
                    requiredRight.add(rightShuffleColumns.get(idx));
                }
                return createShuffleJoinRequiredProperties(requiredLeft, requiredRight);
            } else {
                return createShuffleJoinRequiredProperties(leftShuffleColumns, rightShuffleColumns);
            }
        }
    }

    // The bucket positions of a UNION ALL whose children all keep a LOCAL distribution, shared by
    // ChildOutputPropertyGuarantor (whether to keep the colocated plan) and OutputPropertyDeriver (the union's
    // output distribution). Returns, for each bucket column k, the union positions holding the k-th bucket column
    // in every child, or empty if a child isn't LOCAL, has another number of bucket columns than child 0, or some
    // bucket column is at no common position. Each returned list is non-empty.
    // Like for a colocate join, whether the children's tables share one stable colocate group is checked by
    // ChildOutputPropertyGuarantor only: it replaces the children's distribution when they don't, so
    // OutputPropertyDeriver never sees LOCAL children it hasn't vetted.
    public static Optional<List<List<Integer>>> unionAllBucketPositions(PhysicalSetOperation node,
                                                                       List<PhysicalPropertySet> childrenProperties) {
        List<HashDistributionSpec> childSpecs = Lists.newArrayList();
        for (PhysicalPropertySet property : childrenProperties) {
            DistributionSpec spec = property.getDistributionProperty().getSpec();
            if (!(spec instanceof HashDistributionSpec) ||
                    !((HashDistributionSpec) spec).getHashDistributionDesc().isLocal()) {
                return Optional.empty();
            }
            childSpecs.add((HashDistributionSpec) spec);
        }
        HashDistributionSpec first = childSpecs.get(0);
        int numBucketCols = first.getShuffleColumns().size();
        for (HashDistributionSpec other : childSpecs.subList(1, childSpecs.size())) {
            if (other.getShuffleColumns().size() != numBucketCols) {
                return Optional.empty();
            }
        }

        List<List<DistributionCol>> childColumns = node.getChildOutputColumns().stream()
                .map(PropertyDeriverBase::toDistributionCols).collect(Collectors.toList());
        List<List<Integer>> positions = Lists.newArrayList();
        for (int k = 0; k < numBucketCols; ++k) {
            List<Integer> positionsOfK = Lists.newArrayList();
            for (int position = 0; position < node.getOutputColumnRefOp().size(); ++position) {
                boolean inEveryChild = true;
                for (int i = 0; i < childSpecs.size() && inEveryChild; ++i) {
                    HashDistributionSpec spec = childSpecs.get(i);
                    inEveryChild = spec.getEquivDesc().isConnected(childColumns.get(i).get(position),
                            spec.getShuffleColumns().get(k));
                }
                if (inEveryChild) {
                    positionsOfK.add(position);
                }
            }
            if (positionsOfK.isEmpty()) {
                return Optional.empty();
            }
            positions.add(positionsOfK);
        }
        return Optional.of(positions);
    }

    public static boolean sameColumnIds(List<DistributionCol> left, List<DistributionCol> right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (int i = 0; i < left.size(); ++i) {
            if (left.get(i).getColId() != right.get(i).getColId()) {
                return false;
            }
        }
        return true;
    }

    public static List<DistributionCol> toDistributionCols(List<ColumnRefOperator> columns) {
        return columns.stream().map(col -> new DistributionCol(col.getId(), true)).collect(Collectors.toList());
    }

    public static List<PhysicalPropertySet> computeShuffleSetRequiredProperties(PhysicalSetOperation node) {
        Preconditions.checkArgument(
                !((node instanceof PhysicalUnionOperator) && ((PhysicalUnionOperator) node).isUnionAll()));
        List<PhysicalPropertySet> requiredProperties =
                Lists.newArrayListWithCapacity(node.getChildOutputColumns().size());
        for (int i = 0; i < node.getChildOutputColumns().size(); ++i) {
            List<Integer> columns = node.getChildOutputColumns().get(i).stream().map(ColumnRefOperator::getId)
                    .collect(Collectors.toList());
            HashDistributionSpec distribution = DistributionSpec.createHashDistributionSpec(
                    new HashDistributionDesc(columns, SHUFFLE_JOIN));

            PhysicalPropertySet requiredPropertySet =
                    new PhysicalPropertySet(DistributionProperty.createProperty(distribution));
            requiredProperties.add(requiredPropertySet);
        }
        return requiredProperties;
    }



    // A UNION ALL normally asks its children for a round-robin distribution. When the parent requires a hash
    // distribution, it may instead ask each child to be hash distributed on its own output columns at the same
    // union positions: if all children then turn out to be bucket distributed by one colocate group, the union
    // keeps that bucket distribution and the parent (e.g. a join with another table of the group) can run
    // colocated. Required columns that are not a plain union output column (e.g. computed by the union's
    // projection) are dropped; a hash distribution on a subset of the required columns still satisfies the
    // requirement. Returns empty when the option does not apply.
    // With enable_shuffle_union_all the same option also serves children that aren't colocated: each one is then
    // shuffled on its column at those positions straight into the parent's fragment (see
    // ChildOutputPropertyGuarantor.transToShuffleUnion), instead of being sent round-robin to the union and
    // shuffled once more above it.
    protected static Optional<List<PhysicalPropertySet>> computeColocateUnionAllRequiredProperties(
            PhysicalUnionOperator node, PhysicalPropertySet requiredFromParent, SessionVariable sessionVariable,
            GroupExpression groupExpression) {
        boolean colocate = sessionVariable.isEnableColocateUnionAll() && !sessionVariable.isDisableColocateSet();
        boolean shuffle = sessionVariable.isEnableShuffleUnionAll();
        if ((!colocate && !shuffle) || !node.isUnionAll() || node.hasLimit()) {
            return Optional.empty();
        }
        DistributionSpec requiredSpec = requiredFromParent.getDistributionProperty().getSpec();
        if (!(requiredSpec instanceof HashDistributionSpec)) {
            return Optional.empty();
        }
        HashDistributionDesc requiredDesc = ((HashDistributionSpec) requiredSpec).getHashDistributionDesc();
        if (!requiredDesc.isShuffle()) {
            return Optional.empty();
        }

        Optional<UnionAllRequiredPositions> required = unionAllRequiredPositions(node, requiredDesc);
        if (required.isEmpty()) {
            return Optional.empty();
        }
        List<DistributionCol> requiredCols = required.get().requiredCols;
        List<Integer> positions = required.get().positions;
        // Shuffled children are hashed on exactly these columns, so the union output only satisfies the parent if
        // they cover all of its required columns. Colocated ones are LOCAL, which also satisfies a subset.
        boolean shuffleApplies = shuffle && required.get().coversAll;
        boolean colocateApplies = colocate && childrenAreColocateScansBucketedOn(node, groupExpression, positions);
        if (!shuffleApplies && !colocateApplies) {
            return Optional.empty();
        }

        List<PhysicalPropertySet> requiredProperties = Lists.newArrayList();
        for (List<ColumnRefOperator> childOutputColumns : node.getChildOutputColumns()) {
            List<DistributionCol> childCols = Lists.newArrayList();
            for (int i = 0; i < positions.size(); ++i) {
                childCols.add(requiredCols.get(i).updateColId(childOutputColumns.get(positions.get(i)).getId()));
            }
            HashDistributionSpec childSpec = DistributionSpec.createHashDistributionSpec(
                    new HashDistributionDesc(childCols, requiredDesc.getSourceType()));
            requiredProperties.add(new PhysicalPropertySet(DistributionProperty.createProperty(childSpec)));
        }
        return Optional.of(requiredProperties);
    }

    // The parent's required columns that are plain union output columns, and their union positions.
    public static final class UnionAllRequiredPositions {
        public final List<DistributionCol> requiredCols;
        public final List<Integer> positions;
        // whether every required column is at some union position
        public final boolean coversAll;

        UnionAllRequiredPositions(List<DistributionCol> requiredCols, List<Integer> positions, boolean coversAll) {
            this.requiredCols = requiredCols;
            this.positions = positions;
            this.coversAll = coversAll;
        }
    }

    public static Optional<UnionAllRequiredPositions> unionAllRequiredPositions(PhysicalSetOperation node,
                                                                               HashDistributionDesc requiredDesc) {
        // column id seen by the parent -> position in the union output
        List<ColumnRefOperator> outputColumns = node.getOutputColumnRefOp();
        Map<Integer, Integer> outputIdToPosition = Maps.newHashMap();
        for (int i = 0; i < outputColumns.size(); ++i) {
            outputIdToPosition.put(outputColumns.get(i).getId(), i);
        }
        Map<Integer, Integer> visibleIdToPosition = Maps.newHashMap();
        Projection projection = node.getProjection();
        if (projection == null) {
            visibleIdToPosition.putAll(outputIdToPosition);
        } else {
            for (Map.Entry<ColumnRefOperator, ScalarOperator> entry : projection.getColumnRefMap().entrySet()) {
                if (entry.getValue() instanceof ColumnRefOperator) {
                    Integer position = outputIdToPosition.get(((ColumnRefOperator) entry.getValue()).getId());
                    if (position != null) {
                        visibleIdToPosition.put(entry.getKey().getId(), position);
                    }
                }
            }
        }

        List<DistributionCol> requiredCols = Lists.newArrayList();
        List<Integer> positions = Lists.newArrayList();
        boolean coversAll = true;
        for (DistributionCol col : requiredDesc.getDistributionCols()) {
            Integer position = visibleIdToPosition.get(col.getColId());
            if (position == null) {
                coversAll = false;
            } else if (!positions.contains(position)) {
                positions.add(position);
                requiredCols.add(col);
            }
        }
        if (positions.isEmpty()) {
            return Optional.empty();
        }
        return Optional.of(new UnionAllRequiredPositions(requiredCols, positions, coversAll));
    }

    // The columns each child of a shuffled UNION ALL is hashed on: its own columns at the parent's required union
    // positions, null strict as every exchange is. Empty if the parent's requirement doesn't map to union positions.
    public static Optional<List<List<DistributionCol>>> unionAllShuffleColumns(PhysicalSetOperation node,
                                                                              PhysicalPropertySet requiredFromParent) {
        DistributionSpec requiredSpec = requiredFromParent.getDistributionProperty().getSpec();
        if (!(requiredSpec instanceof HashDistributionSpec) ||
                !((HashDistributionSpec) requiredSpec).getHashDistributionDesc().isShuffle()) {
            return Optional.empty();
        }
        Optional<UnionAllRequiredPositions> required =
                unionAllRequiredPositions(node, ((HashDistributionSpec) requiredSpec).getHashDistributionDesc());
        if (required.isEmpty() || !required.get().coversAll) {
            return Optional.empty();
        }
        List<List<DistributionCol>> shuffleColumns = Lists.newArrayList();
        for (List<ColumnRefOperator> childOutputColumns : node.getChildOutputColumns()) {
            shuffleColumns.add(required.get().positions.stream()
                    .map(position -> new DistributionCol(childOutputColumns.get(position).getId(), true))
                    .collect(Collectors.toList()));
        }
        return Optional.of(shuffleColumns);
    }

    // Cheap pre-check so that a UNION ALL doesn't optimize its children twice for nothing: every child must read
    // one table of a single stable colocate group, through filters and projections only, and have all its bucket
    // columns at union positions the parent asks for. Only then can the children keep one colocated distribution.
    private static boolean childrenAreColocateScansBucketedOn(PhysicalUnionOperator node,
                                                              GroupExpression groupExpression,
                                                              List<Integer> positions) {
        ColocateTableIndex colocateIndex = GlobalStateMgr.getCurrentState().getColocateTableIndex();
        ColocateTableIndex.GroupId groupId = null;
        for (int i = 0; i < groupExpression.arity(); ++i) {
            Map<Integer, Integer> columnMapping = Maps.newHashMap();
            List<ColumnRefOperator> childOutputColumns = node.getChildOutputColumns().get(i);
            for (int position : positions) {
                int colId = childOutputColumns.get(position).getId();
                columnMapping.put(colId, colId);
            }
            LogicalOlapScanOperator scan = findColocateScan(groupExpression.inputAt(i), columnMapping);
            if (scan == null || !(scan.getDistributionSpec() instanceof HashDistributionSpec)) {
                return false;
            }
            ColocateTableIndex.GroupId childGroupId = colocateIndex.getGroup(scan.getTable().getId());
            if (childGroupId == null || colocateIndex.isGroupUnstable(childGroupId) ||
                    (groupId != null && !groupId.equals(childGroupId))) {
                return false;
            }
            groupId = childGroupId;
            // columnMapping now maps scan column ids to the child output column they reach
            for (DistributionCol bucketCol : ((HashDistributionSpec) scan.getDistributionSpec()).getShuffleColumns()) {
                if (!columnMapping.containsKey(bucketCol.getColId())) {
                    return false;
                }
            }
        }
        return true;
    }

    // Walks down filters and projections to a colocate olap scan, rewriting columnMapping (keyed by the column id
    // seen at the current level) to be keyed by the scan's column ids. Columns computed on the way are dropped.
    // In each group it follows one logical expression: the scan if the group has one, else the first. That keeps
    // the walk linear in the depth, at the price of missing a plan that only another expression would allow.
    private static LogicalOlapScanOperator findColocateScan(Group group, Map<Integer, Integer> columnMapping) {
        GroupExpression expression = group.getLogicalExpressions().stream()
                .filter(e -> e.getOp() instanceof LogicalOlapScanOperator)
                .findFirst()
                .orElse(group.getFirstLogicalExpression());
        return findColocateScan(expression, columnMapping);
    }

    private static LogicalOlapScanOperator findColocateScan(GroupExpression expression,
                                                            Map<Integer, Integer> columnMapping) {
        Operator op = expression.getOp();
        if (op.getProjection() != null) {
            remapThroughProjection(op.getProjection().getColumnRefMap(), columnMapping);
        }
        if (op instanceof LogicalOlapScanOperator) {
            ColocateTableIndex colocateIndex = GlobalStateMgr.getCurrentState().getColocateTableIndex();
            LogicalOlapScanOperator scan = (LogicalOlapScanOperator) op;
            return colocateIndex.isColocateTable(scan.getTable().getId()) ? scan : null;
        }
        if (op instanceof LogicalProjectOperator) {
            remapThroughProjection(((LogicalProjectOperator) op).getColumnRefMap(), columnMapping);
            return findColocateScan(expression.inputAt(0), columnMapping);
        }
        if (op instanceof LogicalFilterOperator) {
            return findColocateScan(expression.inputAt(0), columnMapping);
        }
        return null;
    }

    private static void remapThroughProjection(Map<ColumnRefOperator, ScalarOperator> projection,
                                               Map<Integer, Integer> columnMapping) {
        Map<Integer, Integer> remapped = Maps.newHashMap();
        for (Map.Entry<ColumnRefOperator, ScalarOperator> entry : projection.entrySet()) {
            Integer outputColId = columnMapping.get(entry.getKey().getId());
            if (outputColId != null && entry.getValue() instanceof ColumnRefOperator) {
                remapped.put(((ColumnRefOperator) entry.getValue()).getId(), outputColId);
            }
        }
        columnMapping.clear();
        columnMapping.putAll(remapped);
    }

    protected static Optional<HashDistributionDesc> getShuffleJoinHashDistributionDesc(
            PhysicalPropertySet requiredPropertySet) {
        if (!requiredPropertySet.getDistributionProperty().isShuffle()) {
            return Optional.empty();
        }
        HashDistributionDesc requireDistributionDesc =
                ((HashDistributionSpec) requiredPropertySet.getDistributionProperty()
                        .getSpec()).getHashDistributionDesc();
        if (SHUFFLE_JOIN != requireDistributionDesc.getSourceType()) {
            return Optional.empty();
        }

        return Optional.of(requireDistributionDesc);
    }

    private static List<PhysicalPropertySet> createShuffleJoinRequiredProperties(List<DistributionCol> leftColumns,
                                                                                 List<DistributionCol> rightColumns) {
        HashDistributionSpec leftDistribution = DistributionSpec.createHashDistributionSpec(
                new HashDistributionDesc(leftColumns, SHUFFLE_JOIN));
        HashDistributionSpec rightDistribution = DistributionSpec.createHashDistributionSpec(
                new HashDistributionDesc(rightColumns, SHUFFLE_JOIN));

        PhysicalPropertySet leftRequiredPropertySet =
                new PhysicalPropertySet(DistributionProperty.createProperty(leftDistribution));
        PhysicalPropertySet rightRequiredPropertySet =
                new PhysicalPropertySet(DistributionProperty.createProperty(rightDistribution));

        return Lists.newArrayList(leftRequiredPropertySet, rightRequiredPropertySet);
    }

    protected PhysicalPropertySet createPropertySetByDistribution(DistributionSpec distributionSpec) {
        DistributionProperty distributionProperty = DistributionProperty.createProperty(distributionSpec);
        return new PhysicalPropertySet(distributionProperty);
    }

    protected DistributionProperty createShuffleAggProperty(List<DistributionCol> partitionColumns) {
        return DistributionProperty.createProperty(DistributionSpec.createHashDistributionSpec(
                new HashDistributionDesc(partitionColumns, HashDistributionDesc.SourceType.SHUFFLE_AGG)));
    }

    protected PhysicalPropertySet createShuffleAggPropertySet(List<DistributionCol> partitions) {
        HashDistributionDesc desc = new HashDistributionDesc(partitions, HashDistributionDesc.SourceType.SHUFFLE_AGG);
        DistributionProperty property = DistributionProperty.createProperty(DistributionSpec.createHashDistributionSpec(desc));
        return new PhysicalPropertySet(property);
    }

    protected PhysicalPropertySet createGatherPropertySet() {
        DistributionProperty distributionProperty =
                DistributionProperty.createProperty(DistributionSpec.createGatherDistributionSpec());
        return new PhysicalPropertySet(distributionProperty);
    }

    protected List<DistributionCol> enforceNullStrict(List<DistributionCol> cols) {
        return cols.stream().map(DistributionCol::getNullStrictCol).collect(Collectors.toList());
    }
}
