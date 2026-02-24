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

import com.google.common.collect.Lists;
import com.starrocks.analysis.BinaryType;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.Type;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.BinaryPredicateOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import org.junit.Assert;
import org.junit.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class CelonisPredicateStatisticsCalculatorTest {

    @Test
    public void testCelonisHashEqualitySelectivity() {
        ColumnRefOperator c1 = new ColumnRefOperator(0, Type.INT, "c1", true);
        ColumnRefOperator c2 = new ColumnRefOperator(1, Type.INT, "c2", true);

        Statistics statistics = Statistics.builder()
                .addColumnStatistic(c1,
                        ColumnStatistic.builder().setNullsFraction(0.5).setDistinctValuesCount(10).build())
                .addColumnStatistic(c2,
                        ColumnStatistic.builder().setNullsFraction(0.8).setDistinctValuesCount(80).build())
                .setOutputRowCount(10000).build();

        // Non-nullable version
        CallOperator hashC1 = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3, Type.BIGINT,
                Lists.newArrayList(c1));
        CallOperator hashC2 = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3, Type.BIGINT,
                Lists.newArrayList(c2));

        BinaryPredicateOperator binaryPredicateOperator =
                new BinaryPredicateOperator(BinaryType.EQ, hashC1, hashC2);
        Statistics estimatedStatistics =
                PredicateStatisticsCalculator.statisticsCalculate(binaryPredicateOperator, statistics);

        Assert.assertEquals(125, estimatedStatistics.getOutputRowCount(), 0.1);

        // Nullable version
        hashC1 = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.BIGINT,
                Lists.newArrayList(c1));
        hashC2 = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.BIGINT,
                Lists.newArrayList(c2));

        binaryPredicateOperator = new BinaryPredicateOperator(BinaryType.EQ, hashC1, hashC2);
        estimatedStatistics =
                PredicateStatisticsCalculator.statisticsCalculate(binaryPredicateOperator, statistics);

        Assert.assertEquals(12.49, estimatedStatistics.getOutputRowCount(), 0.1);
    }

    private void checkCelonisInStatistics(ColumnRefOperator inColumn, List<ScalarOperator> matches, Statistics statistics,
                                          double expectedRowCount, double expectedNullsFraction,
                                          double expectedDistinctValuesCount, double expectedMin, double expectedMax) {
        ArrayOperator matchArray = new ArrayOperator(Type.ARRAY_INT, true, matches);
        CallOperator celonisIn = new CallOperator(FunctionSet.CELONIS_IN, Type.INT,
                Lists.newArrayList(inColumn, matchArray));
        Statistics celonisInStatistics =
                PredicateStatisticsCalculator.statisticsCalculate(celonisIn, statistics);
        ColumnStatistic inColumnStatistics = celonisInStatistics.getColumnStatistic(inColumn);
        Assert.assertEquals(expectedRowCount, celonisInStatistics.getOutputRowCount(), 0.1);
        Assert.assertEquals(expectedNullsFraction, inColumnStatistics.getNullsFraction(), 0.1);
        Assert.assertEquals(expectedDistinctValuesCount, inColumnStatistics.getDistinctValuesCount(), 0.1);
        Assert.assertEquals(expectedMin, inColumnStatistics.getMinValue(), 0.1);
        Assert.assertEquals(expectedMax, inColumnStatistics.getMaxValue(), 0.1);
    }

    @Test
    public void testCelonisInSelectivity() {
        ColumnRefOperator col1 = new ColumnRefOperator(0, Type.INT, "c1", true);
        ColumnRefOperator col2 = new ColumnRefOperator(1, Type.INT, "c2", true);
        ColumnRefOperator col3 = new ColumnRefOperator(2, Type.INT, "c3", true);
        ColumnRefOperator col4 = new ColumnRefOperator(3, Type.INT, "c4", true);
        ColumnRefOperator col5 = new ColumnRefOperator(4, Type.INT, "c5", true);

        Statistics statistics = Statistics.builder()
                .addColumnStatistic(col1,
                        ColumnStatistic.builder()
                                .setMinValue(1)
                                .setMaxValue(1000)
                                .setNullsFraction(0.5)
                                .setDistinctValuesCount(100)
                                .build())
                .addColumnStatistic(col2,
                        ColumnStatistic.builder()
                                .setMinValue(200)
                                .setMaxValue(600)
                                .setNullsFraction(0.4)
                                .setDistinctValuesCount(30)
                                .build())
                .addColumnStatistic(col3,
                        ColumnStatistic.builder()
                                .setMinValue(400)
                                .setMaxValue(1200)
                                .setNullsFraction(0.1)
                                .setDistinctValuesCount(20)
                                .build())
                .addColumnStatistic(col4,
                        ColumnStatistic.unknown())
                .addColumnStatistic(col5,
                        ColumnStatistic.builder()
                                .setMinValue(Double.NaN)
                                .build())
                .setOutputRowCount(10000).build();

        ConstantOperator const1 = new ConstantOperator(100, Type.INT);
        ConstantOperator const2 = new ConstantOperator(500, Type.INT);
        ConstantOperator const3 = new ConstantOperator(1200, Type.INT);
        ConstantOperator const4 = ConstantOperator.createNull(Type.INT);

        // with constant matches
        checkCelonisInStatistics(col1, Lists.newArrayList(const1, const2), statistics, 100, 0, 2, 100, 500);

        // with constant matches and no overlap
        checkCelonisInStatistics(col1, Lists.newArrayList(const3), statistics, 1, 0, 100, 1, 1000);

        // with constant matches and null
        checkCelonisInStatistics(col1, Lists.newArrayList(const1, const2, const4), statistics, 5100, 0.98, 2, 100, 500);

        // with column matches
        checkCelonisInStatistics(col1, Lists.newArrayList(col2, col3), statistics, 2500, 0, 50, 200, 1000);

        // with column matches and null
        checkCelonisInStatistics(col1, Lists.newArrayList(col2, col3, const4), statistics, 7500, 0.66, 50, 200, 1000);

        // with constant and column matches
        checkCelonisInStatistics(col1, Lists.newArrayList(col2, col3, const1, const2), statistics, 2600, 0, 52, 100, 1000);

        // with unknown column statistics
        checkCelonisInStatistics(col1, Lists.newArrayList(col2, col3, const1, const2, col4), statistics,
                statistics.getOutputRowCount() * 0.5 * StatisticsEstimateCoefficient.IN_PREDICATE_DEFAULT_FILTER_COEFFICIENT,
                0, 100, 1, 1000);

        // with nan column statistics
        checkCelonisInStatistics(col1, Lists.newArrayList(col2, col3, const1, const2, col5), statistics,
                statistics.getOutputRowCount() * 0.5 * StatisticsEstimateCoefficient.IN_PREDICATE_DEFAULT_FILTER_COEFFICIENT,
                0, 100, 1, 1000);
    }

    private static class CelonisInSelectivityScenario {
        private final ColumnRefOperator columnRef = new ColumnRefOperator(1, Type.INT, "c1", true);
        public final double rowCount;
        public final double minValue;
        public final double maxValue;
        public final double distinctValuesCount;
        public final double nullsFraction;
        private final List<Bucket> buckets;
        private final Map<String, Long> mcvs;

        public CelonisInSelectivityScenario(double rowCount, double minValue, double maxValue, double distinctValuesCount,
                                            double nullsFraction,
                                            List<Bucket> buckets, Map<String, Long> mcvs) {
            this.rowCount = rowCount;
            this.minValue = minValue;
            this.maxValue = maxValue;
            this.distinctValuesCount = distinctValuesCount;
            this.nullsFraction = nullsFraction;
            this.buckets = buckets;
            this.mcvs = mcvs;
        }

        public CelonisInSelectivityScenario(double rowCount, double minValue, double maxValue, double distinctValuesCount,
                                            double nullsFraction) {
            this.rowCount = rowCount;
            this.minValue = minValue;
            this.maxValue = maxValue;
            this.distinctValuesCount = distinctValuesCount;
            this.nullsFraction = nullsFraction;
            this.buckets = List.of();
            this.mcvs = Map.of();
        }

        public ColumnStatistic getColumnStatistic() {
            return ColumnStatistic.builder() //
                    .setMinValue(minValue) //
                    .setMaxValue(maxValue) //
                    .setDistinctValuesCount(distinctValuesCount) //
                    .setNullsFraction(nullsFraction) //
                    .setHistogram(new Histogram(buckets, mcvs)) //
                    .build();
        }

        public Statistics getStatistics() {
            return Statistics.builder() //
                    .setOutputRowCount(rowCount) //
                    .addColumnStatistic(columnRef, getColumnStatistic()) //
                    .build();
        }
    }

    @Test
    public void testCelonisInSelectivityWithHistogramOnlyNullMatches() {
        // GIVEN
        final var scenario = new CelonisInSelectivityScenario(1000, 1, 40, 1337, 0.1);
        final var statistics = scenario.getStatistics();

        // Only NULL matches.
        List<ScalarOperator> matches = Lists.newArrayList(
                ConstantOperator.createNull(Type.INT),
                ConstantOperator.createNull(Type.INT),
                ConstantOperator.createNull(Type.INT)
        );

        // WHEN / THEN
        final var expectedRowCount = scenario.rowCount * scenario.nullsFraction;
        checkCelonisInStatistics(scenario.columnRef, matches, statistics, expectedRowCount, 1.0, 0, scenario.minValue,
                scenario.maxValue);
    }

    @Test
    public void testCelonisInSelectivityWithHistogram() {
        // GIVEN
        List<Bucket> buckets = Lists.newArrayList(
                new Bucket(1, 9, 100L, 20L),
                new Bucket(11, 19, 200L, 30L),
                new Bucket(21, 29, 300L, 40L)
        );

        Map<String, Long> mcv = new HashMap<>();
        mcv.put("10", 50L);
        mcv.put("20", 60L);
        mcv.put("30", 70L);
        mcv.put("35", 80L);

        final var scenario = new CelonisInSelectivityScenario(1000, 1, 40, 30, 0.1, buckets, mcv);
        final var statistics = scenario.getStatistics();

        List<ScalarOperator> matches = Lists.newArrayList(
                ConstantOperator.createInt(10),
                ConstantOperator.createInt(15),
                ConstantOperator.createInt(30),
                ConstantOperator.createInt(50), // <-- to be pruned due to column stats.
                ConstantOperator.createNull(Type.INT)
        );

        // WHEN / THEN
        checkCelonisInStatistics(scenario.columnRef, matches, statistics, 307.3, 0.32, 3, 10, 30);
    }

    @Test
    public void testCelonisInSelectivityWithHistogramNoNullMatches() {
        // GIVEN
        List<Bucket> buckets = Lists.newArrayList(
                new Bucket(1, 9, 100L, 20L),
                new Bucket(11, 19, 200L, 30L),
                new Bucket(21, 29, 300L, 40L)
        );

        Map<String, Long> mcv = new HashMap<>();
        mcv.put("10", 50L);
        mcv.put("20", 60L);
        mcv.put("30", 70L);
        mcv.put("35", 80L);

        final var scenario = new CelonisInSelectivityScenario(1000, 1, 40, 30, 0.1, buckets, mcv);
        final var statistics = scenario.getStatistics();

        List<ScalarOperator> matches = Lists.newArrayList(
                ConstantOperator.createInt(10),
                ConstantOperator.createInt(15),
                ConstantOperator.createInt(25)
        );

        // WHEN / THEN
        checkCelonisInStatistics(scenario.columnRef, matches, statistics, 107.67857142857143, 0.0, matches.size(), 10, 25);
    }

    private void checkCelonisMultiInStatistics(List<ColumnRefOperator> multiInColumns, List<List<ScalarOperator>> matches,
                                               Statistics statistics, double expectedRowCount, List<Double> expectedNullsFraction,
                                               List<Double> expectedDistinctValuesCount, List<Double> expectedMin,
                                               List<Double> expectedMax) {

        List<ScalarOperator> columnsStructArgs = new ArrayList<>();
        for (int i = 0; i < multiInColumns.size(); ++i) {
            columnsStructArgs.add(new ConstantOperator(String.format("col%d", i), Type.VARCHAR));
            columnsStructArgs.add(multiInColumns.get(i));
        }
        CallOperator columnsStruct = new CallOperator(FunctionSet.NAMED_STRUCT, Type.ANY_STRUCT, columnsStructArgs);

        List<ScalarOperator> matchesStructArgs = new ArrayList<>();
        for (int i = 0; i < matches.size(); ++i) {
            matchesStructArgs.add(new ConstantOperator(String.format("col%d", i), Type.VARCHAR));
            matchesStructArgs.add(new ArrayOperator(Type.INT, true, matches.get(i)));
        }
        CallOperator matchesStruct = new CallOperator(FunctionSet.NAMED_STRUCT, Type.ANY_STRUCT, matchesStructArgs);
        CallOperator celonisMultiIn = new CallOperator(FunctionSet.CELONIS_MULTI_IN, Type.BOOLEAN,
                Lists.newArrayList(columnsStruct, matchesStruct));
        Statistics celonisMultiInStatistics =
                PredicateStatisticsCalculator.statisticsCalculate(celonisMultiIn, statistics);

        for (int i = 0; i < multiInColumns.size(); ++i) {
            ColumnRefOperator multiInColumn = multiInColumns.get(i);
            ColumnStatistic multiInColumnStatistics = celonisMultiInStatistics.getColumnStatistic(multiInColumn);
            Assert.assertEquals(expectedRowCount, celonisMultiInStatistics.getOutputRowCount(), 0.1);
            Assert.assertEquals(expectedNullsFraction.get(i), multiInColumnStatistics.getNullsFraction(), 0.1);
            Assert.assertEquals(expectedDistinctValuesCount.get(i), multiInColumnStatistics.getDistinctValuesCount(), 0.1);
            Assert.assertEquals(expectedMin.get(i), multiInColumnStatistics.getMinValue(), 0.1);
            Assert.assertEquals(expectedMax.get(i), multiInColumnStatistics.getMaxValue(), 0.1);
        }
    }

    @Test
    public void testCelonisMultiInSelectivity() {
        ColumnRefOperator col1 = new ColumnRefOperator(0, Type.INT, "c1", true);
        ColumnRefOperator col2 = new ColumnRefOperator(1, Type.INT, "c2", true);
        ColumnRefOperator col3 = new ColumnRefOperator(2, Type.INT, "c3", true);
        ColumnRefOperator col4 = new ColumnRefOperator(3, Type.INT, "c4", true);
        ColumnRefOperator col5 = new ColumnRefOperator(4, Type.INT, "c5", true);

        Statistics statistics = Statistics.builder()
                .addColumnStatistic(col1,
                        ColumnStatistic.builder()
                                .setMinValue(1)
                                .setMaxValue(1000)
                                .setNullsFraction(0.5)
                                .setDistinctValuesCount(100)
                                .build())
                .addColumnStatistic(col2,
                        ColumnStatistic.builder()
                                .setMinValue(200)
                                .setMaxValue(600)
                                .setNullsFraction(0.4)
                                .setDistinctValuesCount(30)
                                .build())
                .addColumnStatistic(col3,
                        ColumnStatistic.builder()
                                .setMinValue(400)
                                .setMaxValue(1200)
                                .setNullsFraction(0.0)
                                .setDistinctValuesCount(20)
                                .build())
                .addColumnStatistic(col4,
                        ColumnStatistic.unknown())
                .addColumnStatistic(col5,
                        ColumnStatistic.builder()
                                .setMinValue(Double.NaN)
                                .setMaxValue(1000)
                                .setNullsFraction(0.0)
                                .setDistinctValuesCount(40)
                                .build())
                .setOutputRowCount(10000).build();

        // with overlapping constant matches.
        checkCelonisMultiInStatistics(List.of(col1, col2),
                List.of(List.of(new ConstantOperator(100, Type.INT), new ConstantOperator(800, Type.INT)),
                        List.of(new ConstantOperator(300, Type.INT), new ConstantOperator(500, Type.INT))),
                statistics, 6.66, List.of(0.0, 0.0), List.of(2.0, 2.0), List.of(100.0, 300.0), List.of(800.0, 500.0));

        // with overlapping constant and null matches.
        checkCelonisMultiInStatistics(List.of(col1, col2),
                List.of(List.of(new ConstantOperator(100, Type.INT), ConstantOperator.createNull(Type.INT)),
                        List.of(new ConstantOperator(300, Type.INT), new ConstantOperator(500, Type.INT))),
                statistics, 170.0, List.of(0.98, 0.0), List.of(1.0, 2.0), List.of(100.0, 300.0), List.of(100.0, 500.0));

        // with one overlapping and one non-overlapping constant match.
        checkCelonisMultiInStatistics(List.of(col1, col2),
                List.of(List.of(new ConstantOperator(100, Type.INT), new ConstantOperator(1200, Type.INT)),
                        List.of(new ConstantOperator(500, Type.INT), new ConstantOperator(700, Type.INT))),
                statistics, 3.33, List.of(0.0, 0.0), List.of(1.0, 1.0), List.of(100.0, 500.0), List.of(100.0, 500.0));

        // with one non-overlapping constant match and one non-overlapping null match.
        checkCelonisMultiInStatistics(List.of(col1, col3),
                List.of(List.of(new ConstantOperator(100, Type.INT), new ConstantOperator(1200, Type.INT)),
                        List.of(ConstantOperator.createNull(Type.INT), new ConstantOperator(500, Type.INT))),
                statistics, 1.0, List.of(0.0, 0.0), List.of(0.0, 0.0),
                List.of(Double.POSITIVE_INFINITY, Double.POSITIVE_INFINITY),
                List.of(Double.NEGATIVE_INFINITY, Double.NEGATIVE_INFINITY));

        // with unknown column statistics.
        checkCelonisMultiInStatistics(List.of(col1, col4),
                List.of(List.of(new ConstantOperator(100, Type.INT), new ConstantOperator(800, Type.INT)),
                        List.of(new ConstantOperator(300, Type.INT), new ConstantOperator(500, Type.INT))),
                statistics, 100.0, List.of(0.0, 0.0), List.of(2.0, 1.0), List.of(100.0, Double.NEGATIVE_INFINITY),
                List.of(800.0, Double.POSITIVE_INFINITY));

        // with Nan column statistics.
        checkCelonisMultiInStatistics(List.of(col1, col5),
                List.of(List.of(new ConstantOperator(100, Type.INT), new ConstantOperator(800, Type.INT)),
                        List.of(new ConstantOperator(300, Type.INT), new ConstantOperator(500, Type.INT))),
                statistics, 100.0, List.of(0.0, 0.0), List.of(2.0, 40.0), List.of(100.0, Double.NaN), List.of(800.0, 1000.0));
    }
}
