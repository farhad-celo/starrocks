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

package com.starrocks.sql.optimizer.statistics;

import com.google.common.collect.Ordering;
import com.google.common.collect.Streams;
import com.starrocks.analysis.LargeIntLiteral;
import com.starrocks.metric.celonis.CelonisMetrics;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashCalculationException;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashFunction;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.annotation.Nullable;

/**
 * Projects the MCVs of the arguments of a Celonis hash call into MCVs of the hash output.
 */
public class CelonisHashMcvProjector {

    private CelonisHashMcvProjector() {
    }

    /** One known value of a hash argument and its fraction of the argument's rows. Null denotes SQL NULL. */
    private record KnownValue(@Nullable String value, double rowFraction) {
    }

    /**
     * The known values of one hash argument: its MCVs and, when present, NULL.
     */
    private record ExpressionValues(List<KnownValue> values) {

        /**
         * Converts one argument's MCV counts and null fraction to row fractions. The node row count is a lower bound for
         * the MCV population so sampled or stale histograms cannot inflate the projected frequencies.
         */
        static ExpressionValues from(ColumnStatistic columnStatistic, double rowCount) {
            if (columnStatistic.isUnknown()) {
                return new ExpressionValues(List.of());
            }

            final var values = new ArrayList<KnownValue>();

            // Histograms do not carry NULL as an MCV key, so take this part from the null fraction.
            final var nullFraction = columnStatistic.getNullsFraction();
            if (nullFraction > 0) {
                values.add(new KnownValue(null, nullFraction));
            }

            final var histogram = columnStatistic.getHistogram();
            if (histogram == null || nullFraction >= 1) {
                return new ExpressionValues(values);
            }

            final var collectedRowCount = Math.max(histogram.getTotalRows() / (1 - nullFraction), rowCount);
            histogram.getMCV().forEach((value, valueRowCount) -> values.add(
                    new KnownValue(value, valueRowCount / collectedRowCount)));

            return new ExpressionValues(values);
        }

        /** Whether this argument has no value that can take part in a projected MCV. */
        boolean isEmpty() {
            return values.isEmpty();
        }
    }

    /** An ordered, possibly partial argument tuple and its estimated row fraction. */
    private record ValueCombination(List<String> values, double rowFraction) {
        ValueCombination append(KnownValue expressionValue) {
            final var appendedValues = new ArrayList<>(values);
            appendedValues.add(expressionValue.value());
            return new ValueCombination(appendedValues, rowFraction * expressionValue.rowFraction());
        }
    }

    /**
     * Projects the MCVs and NULL frequency of a single argument through a Celonis hash. Returns null when projection is
     * disabled or unsupported.
     */
    @Nullable
    public static Histogram projectSingleArgHash(CallOperator callOperator, ColumnStatistic columnStatistic,
                                                 double rowCount) {
        if (ConnectContext.get() == null || !ConnectContext.get().getSessionVariable().getEnableCelonisHashMcvs()) {
            return null;
        }

        final var hashFunction = CelonisHashFunction.of(callOperator);

        if (hashFunction == null) {
            // There is no Java implementation of the hash function, hence we can not project the MCVs.
            return null;
        }

        if (!callOperator.getChild(0).getType().isVarchar()) {
            // For now, we only support this for VARCHAR invocations (i.e. non-array inputs).
            return null;
        }

        final var projectedMcvs = new HashMap<String, Long>();
        // Project the NULL MCV
        if (hashFunction.computeNull() != null) {
            final var projectedNullRowCount = (long) (rowCount * columnStatistic.getNullsFraction());
            if (projectedNullRowCount > 0) {
                projectedMcvs.put(hashFunction.computeNull().toString(), projectedNullRowCount);
            }
        }

        final var histogram = columnStatistic.getHistogram();
        if (histogram != null) {
            // Project other (non-null) MCVs
            projectedMcvs.putAll(histogram.getMCV() //
                    .entrySet() //
                    .stream() //
                    .collect(Collectors.toMap(key -> hashFunction.compute(key.getKey()).toString(), Map.Entry::getValue)));
        }

        CelonisMetrics.increaseCounter("celonis_hash_mcv_propagation", "Amount of propagated Celonis hash MCVs");
        return new Histogram(List.of(), projectedMcvs);
    }

    /**
     * Projects multi-argument hash MCVs by assuming the arguments are independent. Candidate frequencies are multiplied
     * as fractions and then scaled to this node's row count. The result is limited to the configured number of MCVs and
     * includes one bucket for rows not covered by them. Returns null when projection is disabled, unsupported, or no
     * complete value combination can be constructed.
     *
     * @throws CelonisHashCalculationException if a projected value combination cannot be hashed
     */
    @Nullable
    public static Histogram projectMultiArgHash(CallOperator callOperator,
                                                List<ColumnStatistic> childrenColumnStatistics, double rowCount)
            throws CelonisHashCalculationException {
        if (ConnectContext.get() == null) {
            return null;
        }

        final var sessionVariable = ConnectContext.get().getSessionVariable();
        if (!sessionVariable.getEnableCelonisHashMcvs() || !sessionVariable.getEnableCelonisHashMcvsMultiArg()) {
            return null;
        }

        final var mcvLimit = sessionVariable.getCelonisHashMcvsMultiArgLimitMcvs();
        if (mcvLimit <= 0 || rowCount <= 0) {
            return null;
        }

        final var hashFunction = CelonisHashFunction.of(callOperator);
        if (hashFunction == null) {
            // There is no Java implementation of the hash function, hence we can not project the MCVs.
            return null;
        }

        if (callOperator.getChildren().stream().anyMatch(argument -> !argument.getType().isVarchar())) {
            // For now, we only support this for VARCHAR arguments (i.e. non-array inputs). A combination needs a value
            // of every argument, so a single unsupported argument leaves nothing to project.
            return null;
        }

        final var hasUnrelatableMcvs = Streams.zip(callOperator.getChildren().stream(), //
                    childrenColumnStatistics.stream(), //
                    CelonisHashMcvProjector::hasMcvsWithoutBuckets) //
                    .anyMatch(Boolean::booleanValue);
        if (hasUnrelatableMcvs) {
            return null;
        }

        final var expressionValues = childrenColumnStatistics.stream() //
                .map(statistic -> ExpressionValues.from(statistic, rowCount)) //
                .toList();

        if (expressionValues.stream().anyMatch(ExpressionValues::isEmpty)) {
            // A complete combination needs one known value from every argument.
            return null;
        }

        // Bound intermediate and final Cartesian products by retaining only the most frequent combinations.
        var combinations = List.of(new ValueCombination(List.of(), 1.0));
        for (final var values : expressionValues) {
            combinations = combineWithExpressionValues(combinations, values, mcvLimit);
        }

        final var projectedMcvs = new HashMap<String, Long>();
        for (final var combination : combinations) {
            final var combinationRowCount =
                    (long) Math.floor(combination.rowFraction() * rowCount);
            if (combinationRowCount > 0) {
                final var hash = hashFunction.compute(combination.values().toArray(new String[0]));
                projectedMcvs.merge(hash.toString(), combinationRowCount, Long::sum);
            }
        }

        if (projectedMcvs.isEmpty()) {
            return null;
        }

        // Keep one full-range bucket for the rows not covered by projected MCVs.
        final var mcvRowCount = projectedMcvs.values().stream().mapToLong(Long::longValue).sum();
        final var nonMcvRowCount = Math.max(0L, Math.round(rowCount) - mcvRowCount);
        final var buckets = nonMcvRowCount > 0 //
                ? List.of(new Bucket(LargeIntLiteral.LARGE_INT_MIN.doubleValue(),
                        LargeIntLiteral.LARGE_INT_MAX.doubleValue(), nonMcvRowCount, 0L)) //
                : List.<Bucket>of();

        CelonisMetrics.increaseCounter("celonis_hash_mcv_propagation_multi_arg",
                "Amount of Celonis multi-argument hash calls whose MCVs were propagated");
        return new Histogram(buckets, projectedMcvs);
    }

    /**
     * Whether the MCV counts of an argument cannot be related to the rows they were counter over. Histograms without
     * buckets report no more than their MCV rows as total rows, which understates that population and would inflate the
     * projected frequencies. Constants are exempt as their statistics are derived from this node's row count.
     */
    private static boolean hasMcvsWithoutBuckets(ScalarOperator argument, ColumnStatistic columnStatistic) {
        if (argument.isConstant()) {
            return false;
        }
        final var histogram = columnStatistic.getHistogram();
        return histogram != null && histogram.getBuckets().isEmpty();
    }

    /**
     * Extends every combination by every value of the next argument and returns the most frequent of the results.
     */
    private static List<ValueCombination> combineWithExpressionValues(List<ValueCombination> combinations,
                                                                      ExpressionValues expressionValues,
                                                                      int mcvLimit) {
        final var byFrequency = Ordering.from(Comparator.comparingDouble(ValueCombination::rowFraction));
        return byFrequency.greatestOf(
              combinations.stream()
                      .flatMap(combination -> expressionValues.values().stream()
                              .map(combination::append))
                      .iterator(),
              mcvLimit);
    }
}
