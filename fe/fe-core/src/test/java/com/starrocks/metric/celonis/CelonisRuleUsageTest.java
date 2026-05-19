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

package com.starrocks.metric.celonis;

import com.starrocks.common.FeConstants;
import com.starrocks.metric.Metric;
import com.starrocks.metric.MetricLabel;
import com.starrocks.metric.MetricRepo;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class CelonisRuleUsageTest {

    @BeforeAll
    public static void beforeClass() {
        FeConstants.runningUnitTest = true;
        MetricRepo.init();
    }

    /**
     * Helper class for mocking query optimizer rules (e.g. SkewJoinOptimizeRule)
     * Normally each different rule may have different reasons,
     * but we share the enums across every test rule in this test.
     */
    private static class TestRule {
        final String ruleName;
        final String ruleCategory;

        enum TriggerReason { OKAY, LUCKY }

        enum NoTriggerReason { FAIL, UNKNOWN }

        final CelonisRuleUsageMetrics<TriggerReason, NoTriggerReason> metrics;

        public TestRule(String ruleName, String ruleCategory) {
            this.ruleName = ruleName;
            this.ruleCategory = ruleCategory;
            this.metrics = new CelonisRuleUsageMetrics<>(ruleName, ruleCategory);
        }
    }

    private static List<Metric> filterByLabels(List<Metric> metrics, MetricLabel... labels) {
        return metrics.stream().filter(metric -> metric.getLabels().containsAll(Arrays.asList(labels)))
                .collect(Collectors.toList());
    }

    private static long sum(List<Metric> metrics) {
        return metrics.stream().mapToLong(metric -> ((Number) metric.getValue()).longValue()).sum();
    }

    @Test
    public void testSingleRule() {
        // GIVEN
        final var ruleName = "test_rule";
        final var ruleCategory = "test_category_1";
        final var rule = new TestRule(ruleName, ruleCategory);

        // WHEN
        rule.metrics.triggered(TestRule.TriggerReason.OKAY);
        rule.metrics.triggered(TestRule.TriggerReason.OKAY);
        rule.metrics.triggered(TestRule.TriggerReason.OKAY);
        rule.metrics.triggered(TestRule.TriggerReason.LUCKY);

        // THEN
        final var checked = MetricRepo.getMetricsByName(ruleCategory + "_checked");
        final var triggered = MetricRepo.getMetricsByName(ruleCategory + "_triggered");
        final var notTriggered = MetricRepo.getMetricsByName(ruleCategory + "_not_triggered");

        assertEquals(1, checked.size());      // [ (rule) ]
        assertEquals(2, triggered.size());    // [ (rule; okay), (rule; lucky) ]
        assertEquals(0, notTriggered.size()); // [ ]

        assertEquals(4L, sum(checked));
        assertEquals(4L, sum(triggered));
        assertEquals(3L, sum(filterByLabels(triggered, new MetricLabel("reason", TestRule.TriggerReason.OKAY.name()))));
        assertEquals(1L, sum(filterByLabels(triggered, new MetricLabel("reason", TestRule.TriggerReason.LUCKY.name()))));
        assertEquals(0L, sum(notTriggered));
    }

    @Test
    public void testMultipleRules() {
        // GIVEN
        final var category = "test_category_2";
        final var rule1 = new TestRule("test_rule_1", category);
        final var rule2 = new TestRule("test_rule_2", category);
        final var rule3 = new TestRule("test_rule_3", category);
        final var rule4 = new TestRule("test_rule_4", category);

        // WHEN
        rule1.metrics.notTriggered(TestRule.NoTriggerReason.FAIL);
        rule1.metrics.notTriggered(TestRule.NoTriggerReason.FAIL);
        rule1.metrics.notTriggered(TestRule.NoTriggerReason.UNKNOWN);

        rule2.metrics.triggered(TestRule.TriggerReason.OKAY);
        rule2.metrics.notTriggered(TestRule.NoTriggerReason.FAIL);

        rule3.metrics.triggered(TestRule.TriggerReason.OKAY);
        rule3.metrics.triggered(TestRule.TriggerReason.LUCKY);
        rule3.metrics.triggered(TestRule.TriggerReason.LUCKY);
        rule3.metrics.notTriggered(TestRule.NoTriggerReason.UNKNOWN);

        // THEN
        final var checked = MetricRepo.getMetricsByName(category + "_checked");
        final var triggered = MetricRepo.getMetricsByName(category + "_triggered");
        final var notTriggered = MetricRepo.getMetricsByName(category + "_not_triggered");

        assertEquals(3, checked.size());      // [ (rule1), (rule2), (rule3)]
        assertEquals(3, triggered.size());    // [ (rule2; okay), (rule3; okay), (rule3; lucky) ]
        assertEquals(4, notTriggered.size()); // [ (rule1; fail), (rule1; unknown), (rule2; fail), (rule3; unknown) ]

        assertEquals(9, sum(checked));      // total checks under the category
        assertEquals(4, sum(triggered));    // total triggers under the category
        assertEquals(5, sum(notTriggered)); // total no-triggers under the category

        // total checks per rule
        assertEquals(3, sum(filterByLabels(checked, new MetricLabel("rule", rule1.ruleName))));
        assertEquals(2, sum(filterByLabels(checked, new MetricLabel("rule", rule2.ruleName))));
        assertEquals(4, sum(filterByLabels(checked, new MetricLabel("rule", rule3.ruleName))));
        assertEquals(0, sum(filterByLabels(checked, new MetricLabel("rule", rule4.ruleName))));

        // total triggers per rule
        assertEquals(0, sum(filterByLabels(triggered, new MetricLabel("rule", rule1.ruleName))));
        assertEquals(1, sum(filterByLabels(triggered, new MetricLabel("rule", rule2.ruleName))));
        assertEquals(3, sum(filterByLabels(triggered, new MetricLabel("rule", rule3.ruleName))));
        assertEquals(0, sum(filterByLabels(triggered, new MetricLabel("rule", rule4.ruleName))));

        // total no-triggers per rule
        assertEquals(3, sum(filterByLabels(notTriggered, new MetricLabel("rule", rule1.ruleName))));
        assertEquals(1, sum(filterByLabels(notTriggered, new MetricLabel("rule", rule2.ruleName))));
        assertEquals(1, sum(filterByLabels(notTriggered, new MetricLabel("rule", rule3.ruleName))));
        assertEquals(0, sum(filterByLabels(notTriggered, new MetricLabel("rule", rule4.ruleName))));

        final var all = Stream.of(checked, triggered, notTriggered)
                .flatMap(Collection::stream)
                .collect(Collectors.toList());

        assertEquals(2, sum(filterByLabels(all, new MetricLabel("reason", TestRule.TriggerReason.OKAY.name()))));
        assertEquals(2, sum(filterByLabels(all, new MetricLabel("reason", TestRule.TriggerReason.LUCKY.name()))));
        assertEquals(3, sum(filterByLabels(all, new MetricLabel("reason", TestRule.NoTriggerReason.FAIL.name()))));
        assertEquals(2, sum(filterByLabels(all, new MetricLabel("reason", TestRule.NoTriggerReason.UNKNOWN.name()))));
    }
}
