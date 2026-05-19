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

import com.starrocks.metric.MetricLabel;

public class CelonisRuleUsageMetrics<
        TriggerReason extends Enum<TriggerReason>,
        NoTriggerReason extends Enum<NoTriggerReason>> {
    private final String ruleName;
    private final String ruleCategory;

    public CelonisRuleUsageMetrics(String ruleName, String ruleCategory) {
        this.ruleName = ruleName;
        this.ruleCategory = ruleCategory;
    }

    private void checked() {
        final String metricName = this.ruleCategory + "_checked";
        final String metricDesc = "Number of times " + this.ruleCategory + " were checked";
        CelonisMetrics.increaseCounter(metricName, metricDesc, new MetricLabel("rule", ruleName));
    }

    public void triggered(TriggerReason reason) {
        final String metricName = this.ruleCategory + "_triggered";
        final String metricDesc = "Number of times " + this.ruleCategory + " were checked and triggered";
        checked();
        CelonisMetrics.increaseCounter(metricName, metricDesc, new MetricLabel("rule", ruleName),
                new MetricLabel("reason", reason.name()));
    }

    public void notTriggered(NoTriggerReason reason) {
        final String metricName = this.ruleCategory + "_not_triggered";
        final String metricDesc = "Number of times " + this.ruleCategory + " were checked but not triggered";
        checked();
        CelonisMetrics.increaseCounter(metricName, metricDesc, new MetricLabel("rule", ruleName),
                new MetricLabel("reason", reason.name()));
    }
}

