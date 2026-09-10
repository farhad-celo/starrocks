// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.sql.optimizer;

import com.starrocks.catalog.Type;
import com.starrocks.metric.MetricLabel;
import com.starrocks.metric.MetricRepo;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.optimizer.base.Ordering;
import com.starrocks.sql.optimizer.operator.logical.LogicalValuesOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalWindowOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rule.transformation.WindowSkewToMergeSortRule;
import com.starrocks.sql.optimizer.rule.transformation.WindowSkewToMergeSortRule.NoTriggerReason;
import com.starrocks.sql.optimizer.rule.transformation.WindowSkewToMergeSortRule.TriggerReason;
import com.starrocks.sql.optimizer.statistics.ColumnStatistic;
import com.starrocks.sql.optimizer.statistics.Statistics;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class WindowSkewToMergeSortRuleMetricsTest {
    private static final String RULE_NAME = "window_skew_to_merge_sort_rule";
    private static final String TRIGGERED_METRIC = "celonis_skew_rules_triggered";
    private static final String NOT_TRIGGERED_METRIC = "celonis_skew_rules_not_triggered";

    private static final ColumnRefOperator PARTITION_COLUMN_1 =
            new ColumnRefOperator(1, Type.INT, "p1", true);
    private static final ColumnRefOperator PARTITION_COLUMN_2 =
            new ColumnRefOperator(2, Type.INT, "p2", true);
    private static final ColumnRefOperator ORDER_COLUMN =
            new ColumnRefOperator(3, Type.INT, "o", true);

    @BeforeAll
    static void beforeClass() {
        MetricRepo.init();
    }

    @Test
    void testRuleUsageMetrics() {
        // GIVEN
        ConnectContext connectContext = new ConnectContext();
        connectContext.setThreadLocalInfo();
        connectContext.getSessionVariable().setDataSkewRowPercentageThreshold(0.2);
        OptimizerContext optimizerContext = new OptimizerContext(connectContext);
        WindowSkewToMergeSortRule rule = WindowSkewToMergeSortRule.getInstance();
        OptExpression triggeredInput = expression(List.of(PARTITION_COLUMN_1, PARTITION_COLUMN_2));
        OptExpression notTriggeredInput = expression(List.of(PARTITION_COLUMN_1));
        long triggeredBefore = metricValue(TRIGGERED_METRIC, TriggerReason.SKEWED_NULL);
        long notTriggeredBefore = metricValue(NOT_TRIGGERED_METRIC, NoTriggerReason.SINGLE_PARTITION_COLUMN);

        // WHEN
        assertTrue(rule.check(triggeredInput, optimizerContext));
        List<OptExpression> result = rule.transform(triggeredInput, optimizerContext);
        assertFalse(rule.check(notTriggeredInput, optimizerContext));

        // THEN
        assertEquals(1, result.size());
        assertEquals(triggeredBefore + 1, metricValue(TRIGGERED_METRIC, TriggerReason.SKEWED_NULL));
        assertEquals(notTriggeredBefore + 1,
                metricValue(NOT_TRIGGERED_METRIC, NoTriggerReason.SINGLE_PARTITION_COLUMN));
    }

    private static OptExpression expression(List<ScalarOperator> partitionExpressions) {
        LogicalWindowOperator window = new LogicalWindowOperator.Builder()
                .setWindowCall(Map.of())
                .setPartitionExpressions(partitionExpressions)
                .setOrderByElements(List.of(new Ordering(ORDER_COLUMN, true, true)))
                .setEnforceSortColumns(List.of())
                .setSkewValues(List.of())
                .build();
        OptExpression child = OptExpression.create(
                new LogicalValuesOperator(List.of(PARTITION_COLUMN_1, PARTITION_COLUMN_2, ORDER_COLUMN)));
        child.setStatistics(Statistics.builder()
                .setOutputRowCount(1000)
                .addColumnStatistic(PARTITION_COLUMN_1, skewedStatistic())
                .addColumnStatistic(PARTITION_COLUMN_2, skewedStatistic())
                .build());
        return OptExpression.create(window, child);
    }

    private static ColumnStatistic skewedStatistic() {
        return ColumnStatistic.builder().setNullsFraction(0.3).build();
    }

    private static long metricValue(String metricName, Enum<?> reason) {
        List<MetricLabel> labels = Arrays.asList(
                new MetricLabel("rule", RULE_NAME), new MetricLabel("reason", reason.name()));
        return MetricRepo.getMetricsByName(metricName).stream()
                .filter(metric -> metric.getLabels().containsAll(labels))
                .mapToLong(metric -> ((Number) metric.getValue()).longValue())
                .sum();
    }
}
