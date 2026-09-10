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

package com.starrocks.sql.optimizer.rule.transformation;

import com.google.common.collect.Lists;
import com.starrocks.metric.celonis.CelonisRuleUsageMetrics;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.OptimizerContext;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.logical.LogicalWindowOperator;
import com.starrocks.sql.optimizer.operator.pattern.Pattern;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rule.RuleType;
import com.starrocks.sql.optimizer.skew.DataSkew;
import com.starrocks.sql.optimizer.statistics.Statistics;

import java.util.Collections;
import java.util.List;

/*
 * Rule Objective:
 *
 * When a window analytic operator has a skewed partition key, set forceMergeSort = true.
 * This causes the RequiredPropertyDeriver to use GatherDistributionSpec instead of hash shuffle,
 * which sorts the data first using merge-exchange, ensuring that workload during sort is spread evenly.
 *
 * Controlled by the enable_window_skew_merge_sort session variable, which enables statistics-based
 * skew detection. The [merge_sort] window hint sets the same flag directly, independent of this rule.
 */
public class WindowSkewToMergeSortRule extends TransformationRule {

    private static final WindowSkewToMergeSortRule INSTANCE = new WindowSkewToMergeSortRule();
    private static final String RULE_NAME = "window_skew_to_merge_sort_rule";
    private static final String RULE_CATEGORY = "celonis_skew_rules";

    public enum TriggerReason { SKEWED_NULL, SKEWED_MCV, SKEWED_NULL_AND_MCV }

    public enum NoTriggerReason {
        NO_PARTITION_COLUMN,
        SINGLE_PARTITION_COLUMN,
        NO_ORDER_BY_COLUMN,
        ALREADY_FORCE_MERGE_SORT,
        SKEW_HINT_PRESENT,
        MISSING_STATS,
        NON_COLUMN_REF_PARTITION_EXPRESSION,
        NOT_SKEWED,
        INACCURATE_ROW_COUNT,
        NO_MCV,
        NO_HISTOGRAM,
    }

    public static final CelonisRuleUsageMetrics<TriggerReason, NoTriggerReason>
            RULE_USAGE_METRICS = new CelonisRuleUsageMetrics<>(RULE_NAME, RULE_CATEGORY);

    private WindowSkewToMergeSortRule() {
        super(RuleType.TF_WINDOW_SKEW_TO_MERGE_SORT, Pattern.create(OperatorType.LOGICAL_WINDOW));
    }

    public static WindowSkewToMergeSortRule getInstance() {
        return INSTANCE;
    }

    @Override
    public boolean check(OptExpression input, OptimizerContext context) {
        if (input.getOp() instanceof LogicalWindowOperator lwo) {
            List<ScalarOperator> partitionExprs = lwo.getPartitionExpressions();
            if (partitionExprs == null || partitionExprs.isEmpty()) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NO_PARTITION_COLUMN);
            } else if (partitionExprs.size() == 1) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.SINGLE_PARTITION_COLUMN);
            } else if (lwo.getOrderByElements() == null || lwo.getOrderByElements().isEmpty()) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NO_ORDER_BY_COLUMN);
            } else if (lwo.isForceMergeSort()) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.ALREADY_FORCE_MERGE_SORT);
            } else if (lwo.isSkewed() || lwo.getSkewColumn() != null) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.SKEW_HINT_PRESENT);
            }

            return partitionExprs != null
                    && partitionExprs.size() > 1
                    && lwo.getOrderByElements() != null
                    && !lwo.getOrderByElements().isEmpty()
                    && !lwo.isForceMergeSort()
                    && !lwo.isSkewed()
                    && lwo.getSkewColumn() == null;
        }
        return false;
    }

    @Override
    public List<OptExpression> transform(OptExpression input, OptimizerContext context) {
        LogicalWindowOperator window = (LogicalWindowOperator) input.getOp();

        // Trigger only if every individual partition column shows skew from child statistics.
        OptExpression child = input.inputAt(0);
        Statistics statistics = child.getStatistics();
        if (statistics == null) {
            RULE_USAGE_METRICS.notTriggered(NoTriggerReason.MISSING_STATS);
            return Collections.emptyList();
        }

        if (!allPartitionColumnsSkewed(window, statistics, context)) {
            return Collections.emptyList();
        }

        return buildResult(window, input);
    }

    private boolean allPartitionColumnsSkewed(LogicalWindowOperator window, Statistics statistics,
                                              OptimizerContext context) {
        double threshold = context.getSessionVariable().getDataSkewRowPercentageThreshold();
        DataSkew.Thresholds thresholds = DataSkew.Thresholds.withRelativeRowThreshold(threshold);
        boolean hasNullSkew = false;
        boolean hasMcvSkew = false;

        for (ScalarOperator partitionExpr : window.getPartitionExpressions()) {
            // A partition column we cannot analyze cannot be considered skewed, so it prevents
            // the rule from triggering when we require every column to be skewed.
            if (!(partitionExpr instanceof ColumnRefOperator col)) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NON_COLUMN_REF_PARTITION_EXPRESSION);
                return false;
            }
            if (!statistics.getColumnStatistics().containsKey(col)) {
                RULE_USAGE_METRICS.notTriggered(NoTriggerReason.MISSING_STATS);
                return false;
            }

            final var skewInfo =
                    DataSkew.getColumnSkewInfo(statistics, statistics.getColumnStatistic(col), thresholds);
            if (!skewInfo.isSkewed()) {
                reportNotTriggered(skewInfo.additionalInfo());
                return false;
            }

            if (skewInfo.type() == DataSkew.SkewType.SKEWED_NULL) {
                hasNullSkew = true;
            } else if (skewInfo.type() == DataSkew.SkewType.SKEWED_MCV) {
                hasMcvSkew = true;
            }
        }

        if (hasNullSkew && hasMcvSkew) {
            RULE_USAGE_METRICS.triggered(TriggerReason.SKEWED_NULL_AND_MCV);
        } else if (hasNullSkew) {
            RULE_USAGE_METRICS.triggered(TriggerReason.SKEWED_NULL);
        } else if (hasMcvSkew) {
            RULE_USAGE_METRICS.triggered(TriggerReason.SKEWED_MCV);
        }
        return true;
    }

    private static void reportNotTriggered(DataSkew.AdditionalInfo additionalInfo) {
        switch (additionalInfo) {
            case NONE -> RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NOT_SKEWED);
            case UNKNOWN_STATS -> RULE_USAGE_METRICS.notTriggered(NoTriggerReason.MISSING_STATS);
            case INACCURATE_ROW_COUNT -> RULE_USAGE_METRICS.notTriggered(NoTriggerReason.INACCURATE_ROW_COUNT);
            case NO_MCV -> RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NO_MCV);
            case NO_HISTOGRAM -> RULE_USAGE_METRICS.notTriggered(NoTriggerReason.NO_HISTOGRAM);
        }
    }

    private List<OptExpression> buildResult(LogicalWindowOperator originalWindow, OptExpression input) {
        LogicalWindowOperator newWindow = new LogicalWindowOperator.Builder()
                .withOperator(originalWindow)
                .setForceMergeSort(true)
                .setUseHashBasedPartition(false)
                .setIsSkewed(false)
                .setSkewColumn(null)
                .setSkewValues(Collections.emptyList())
                .build();
        return Lists.newArrayList(OptExpression.create(newWindow, input.getInputs()));
    }
}
