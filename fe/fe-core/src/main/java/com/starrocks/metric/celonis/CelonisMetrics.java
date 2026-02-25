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

import com.starrocks.metric.LongCounterMetric;
import com.starrocks.metric.Metric;
import com.starrocks.metric.MetricLabel;
import com.starrocks.metric.MetricRepo;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

public class CelonisMetrics {
    private static final ConcurrentHashMap<CelonisMetric, LongCounterMetric> COUNTERS = new ConcurrentHashMap<>();

    private static class CelonisMetric {
        private final String key;
        private final String description;
        private final List<MetricLabel> labels;

        public CelonisMetric(String key, String description, MetricLabel... labels) {
            this.key = key;
            this.description = description;
            this.labels = new ArrayList<>(Arrays.asList(labels)); // Copy to avoid pointers to the array.
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) {
                return true;
            }
            if (obj == null || getClass() != obj.getClass()) {
                return false;
            }
            CelonisMetric other = (CelonisMetric) obj;

            // Only consider key and labels, disregard description.
            return Objects.equals(key, other.key) &&
                    Objects.equals(labels, other.labels);
        }

        @Override
        public int hashCode() {
            // Only consider key and labels, disregard description.
            return Objects.hash(key, labels);
        }
    }

    public static void increaseCounter(String key, String description, MetricLabel... labels) {
        final var metricModel = new CelonisMetric(key, description, labels);
        COUNTERS.computeIfAbsent(metricModel, model -> {
            final var metric = new LongCounterMetric(model.key, Metric.MetricUnit.NOUNIT, model.description);
            model.labels.forEach(metric::addLabel);
            MetricRepo.addMetric(metric);
            return metric;
        }).increase(1L);
    }
}
