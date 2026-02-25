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

import com.google.common.collect.Sets;
import com.starrocks.common.FeConstants;
import com.starrocks.metric.MetricLabel;
import com.starrocks.metric.MetricRepo;
import org.junit.BeforeClass;
import org.junit.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CelonisMetricTest {

    @BeforeClass
    public static void beforeClass() {
        FeConstants.runningUnitTest = true;
        MetricRepo.init();
    }

    @Test
    public void itShouldAddCounterMetricIfNotExistent() {
        // GIVEN
        final var metricKey = "some.key1";

        // WHEN
        CelonisMetrics.increaseCounter(metricKey, "desc");

        // THEN
        final var actualMetrics = MetricRepo.getMetricsByName(metricKey);
        assertEquals(1, actualMetrics.size());
        assertEquals(1L, actualMetrics.get(0).getValue());
    }

    @Test
    public void itShouldIncreaseCounterMetricIfExistent() {
        // GIVEN
        final var metricKey = "some.key2";
        CelonisMetrics.increaseCounter(metricKey, "desc");

        // WHEN
        CelonisMetrics.increaseCounter(metricKey, "desc");

        // THEN
        final var actualMetrics = MetricRepo.getMetricsByName(metricKey);
        assertEquals(1, actualMetrics.size());
        assertEquals(2L, actualMetrics.get(0).getValue());
    }

    @Test
    public void itShouldRecreateCounterMetricForDifferentLabels() {
        // GIVEN
        final var metricKey = "some.key3";
        final var secondLabels = List.of(new MetricLabel("label1", "label1"));
        final var thirdLabels = List.of(new MetricLabel("label2", "label2"));
        final var fourthLabels = List.of(new MetricLabel("label3", "label3"), new MetricLabel("label4", "label4"));

        // WHEN
        CelonisMetrics.increaseCounter(metricKey, "desc"); // no label
        CelonisMetrics.increaseCounter(metricKey, "desc", secondLabels.toArray(new MetricLabel[0]));
        CelonisMetrics.increaseCounter(metricKey, "desc", thirdLabels.toArray(new MetricLabel[0]));
        CelonisMetrics.increaseCounter(metricKey, "desc", fourthLabels.toArray(new MetricLabel[0]));
        CelonisMetrics.increaseCounter(metricKey, "desc", fourthLabels.toArray(new MetricLabel[0]));

        // THEN
        final var actualMetrics = MetricRepo.getMetricsByName(metricKey);
        assertEquals(4, actualMetrics.size());

        final var expectedLabels = Sets.newHashSet(List.of(), secondLabels, thirdLabels, fourthLabels);
        for (final var actualMetric : actualMetrics) {
            assertEquals(metricKey, actualMetric.getName());
            assertEquals("desc", actualMetric.getDescription());

            assertTrue(expectedLabels.contains(actualMetric.getLabels()));

            if (actualMetric.getLabels().equals(fourthLabels)) {
                assertEquals(2L, actualMetric.getValue());
            }

            expectedLabels.remove(actualMetric.getLabels());
        }

        assertTrue(expectedLabels.isEmpty());
    }
}
