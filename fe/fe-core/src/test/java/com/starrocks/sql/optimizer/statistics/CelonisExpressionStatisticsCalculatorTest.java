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
import com.starrocks.analysis.LargeIntLiteral;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashFunction;
import com.starrocks.sql.optimizer.rewrite.celonis.XXHASH3128NULLABLE;
import com.starrocks.sql.optimizer.rewrite.celonis.XXHASH3128V3;
import com.starrocks.sql.optimizer.rewrite.celonis.XXHASH3128V4;
import com.starrocks.utframe.UtFrameUtils;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Map;

import static java.lang.Double.NEGATIVE_INFINITY;
import static java.lang.Double.POSITIVE_INFINITY;
import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

public class CelonisExpressionStatisticsCalculatorTest {

    private static class UnaryTestScenario {
        private final ColumnRefOperator intColumnRefOperator;
        private final ColumnRefOperator arrayColumnRefOperator;
        private final ColumnRefOperator stringColumnRefOperator;
        private final ColumnRefOperator stringArrayColumnRefOperator;
        private final Statistics statistics;
        private final Statistics unknownStatistics;
        private final Statistics emptyCollectionStatistics;

        public UnaryTestScenario(ColumnRefOperator intColumnRefOperator, ColumnRefOperator arrayColumnRefOperator,
                                 ColumnRefOperator stringColumnRefOperator, ColumnRefOperator stringArrayColumnRefOperator,
                                 Statistics statistics) {
            this.intColumnRefOperator = intColumnRefOperator;
            this.arrayColumnRefOperator = arrayColumnRefOperator;
            this.stringColumnRefOperator = stringColumnRefOperator;
            this.stringArrayColumnRefOperator = stringArrayColumnRefOperator;
            this.statistics = statistics;

            var unknownStatistics = Statistics.buildFrom(statistics);
            var emptyCollectionStatistics = Statistics.buildFrom(statistics);
            for (final var stat : statistics.getColumnStatistics().entrySet()) {
                unknownStatistics.addColumnStatistic(stat.getKey(), ColumnStatistic.unknown());

                var emptyCollectionColumnStatistics = ColumnStatistic.buildFrom(stat.getValue()) //
                        .setCollectionSize(0) //
                        .build();
                emptyCollectionStatistics.addColumnStatistic(stat.getKey(), emptyCollectionColumnStatistics);
            }
            this.unknownStatistics = unknownStatistics.build();
            this.emptyCollectionStatistics = emptyCollectionStatistics.build();
        }

    }

    private static UnaryTestScenario unaryTestScenario() {
        ColumnRefOperator intColumnRefOperator = new ColumnRefOperator(0, Type.INT, "id", true);
        ColumnRefOperator arrayColumnRefOperator = new ColumnRefOperator(1, Type.ARRAY_INT, "array", true);
        ColumnRefOperator stringColumnRefOperator = new ColumnRefOperator(2, Type.VARCHAR, "string", true);
        ColumnRefOperator stringArrayColumnRefOperator = new ColumnRefOperator(3, Type.ARRAY_VARCHAR, "stringArray", true);
        Statistics.Builder builder = Statistics.builder();
        double min = 0.0;
        double max = 100.0;
        double distinctValue = 80;
        double nullsFraction = 0.2;
        final var statistics = builder.addColumnStatistic(intColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(min) //
                                .setMaxValue(max)
                                .setDistinctValuesCount(distinctValue) //
                                .setNullsFraction(nullsFraction) //
                                .setAverageRowSize(10) //
                                .build()) //
                .addColumnStatistic(arrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(stringArrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(stringColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(10) //
                                .build()) //
                .addColumnStatistic(stringArrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .setOutputRowCount(100) //
                .build();

        return new UnaryTestScenario(intColumnRefOperator, arrayColumnRefOperator, stringColumnRefOperator,
                stringArrayColumnRefOperator, statistics);
    }

    private static class MultiaryTestScenario {
        private final ColumnRefOperator datetimeColumnRefOperator;
        private final ColumnRefOperator stringArrayColumnRefOperator;
        private final ColumnRefOperator dateTimeArrayColumnRefOperator;
        private final ColumnRefOperator intArrayColumnRefOperator;
        private final ColumnRefOperator stringColumnRefOperator;
        private final Statistics statistics;

        public MultiaryTestScenario(ColumnRefOperator datetimeColumnRefOperator, ColumnRefOperator stringArrayColumnRefOperator,
                                    ColumnRefOperator dateTimeArrayColumnRefOperator, ColumnRefOperator intArrayColumnRefOperator,
                                    ColumnRefOperator stringColumnRefOperator,
                                    Statistics statistics) {
            this.datetimeColumnRefOperator = datetimeColumnRefOperator;
            this.stringArrayColumnRefOperator = stringArrayColumnRefOperator;
            this.dateTimeArrayColumnRefOperator = dateTimeArrayColumnRefOperator;
            this.intArrayColumnRefOperator = intArrayColumnRefOperator;
            this.stringColumnRefOperator = stringColumnRefOperator;
            this.statistics = statistics;
        }
    }

    private static MultiaryTestScenario multiaryTestScenario() {
        ColumnRefOperator datetimeColumnRefOperator = new ColumnRefOperator(0, Type.DATETIME, "date", true);
        ColumnRefOperator stringArrayColumnRefOperator = new ColumnRefOperator(1, Type.ARRAY_VARCHAR, "stringArray", true);
        ColumnRefOperator dateTimeArrayColumnRefOperator = new ColumnRefOperator(1, Type.ARRAY_DATETIME, "datetimeArray", true);
        ColumnRefOperator intArrayColumnRefOperator = new ColumnRefOperator(1, Type.ARRAY_INT, "intArray", true);
        ColumnRefOperator stringColumnRefOperator = new ColumnRefOperator(2, Type.VARCHAR, "string", true);
        Statistics statistics = Statistics.builder() //
                .addColumnStatistic(datetimeColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(1.4490589536E12) //
                                .setMaxValue(1.76467824E12) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(10) //
                                .build()) //
                .addColumnStatistic(stringArrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(intArrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(-1337) //
                                .setMaxValue(1337) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(dateTimeArrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(stringColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(10) //
                                .build()) //
                .setOutputRowCount(100) //
                .build(); //

        return new MultiaryTestScenario(datetimeColumnRefOperator, stringArrayColumnRefOperator, dateTimeArrayColumnRefOperator,
                intArrayColumnRefOperator, stringColumnRefOperator, statistics);
    }

    @Test
    public void testCelonisUnaryFunctionCall() {
        ColumnRefOperator columnRefOperator = new ColumnRefOperator(0, Type.INT, "id", true);

        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;

        // test CELONIS_XX_HASH3_128_V3 function
        CallOperator callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3,
                Type.INT, Lists.newArrayList(columnRefOperator));
        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        assertEquals(1.7014118346046923E38, columnStatistic.getMaxValue(), 0.001);
        assertEquals(-1.7014118346046923E38, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        // test CELONIS_XX_HASH3_128_NULLABLE function
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.INT,
                Lists.newArrayList(columnRefOperator));
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        assertEquals(1.7014118346046923E38, columnStatistic.getMaxValue(), 0.001);
        assertEquals(-1.7014118346046923E38, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
    }

    @Test
    public void testCelonisArrayAvg() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_AVG, Type.INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        assertEquals(ScalarType.DOUBLE.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);

        assertEquals(ScalarType.DOUBLE.getTypeSize(), emptyCollectionStatistics.getAverageRowSize(), 0.001);
        assertEquals(-1, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);

    }

    @Test
    public void testCelonisArrayCount() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_COUNT, Type.BIGINT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        for (var stat : List.of(columnStatistic, emptyCollectionStatistics)) {
            assertEquals(POSITIVE_INFINITY, stat.getMaxValue(), 0.001);
            assertEquals(0, stat.getMinValue(), 0.001);
            assertEquals(80, stat.getDistinctValuesCount(), 0.001);
            assertEquals(0.2, stat.getNullsFraction(), 0.001);
            assertEquals(-1, stat.getCollectionSize(), 0.001);
        }
        assertEquals(ColumnStatistic.unknown(), unknownStatistic);
    }

    @Test
    public void testCelonisArrayCountDistinct() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_COUNT_DISTINCT, Type.BIGINT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        for (var stat : List.of(columnStatistic, emptyCollectionStatistics)) {
            assertEquals(POSITIVE_INFINITY, stat.getMaxValue(), 0.001);
            assertEquals(0, stat.getMinValue(), 0.001);
            assertEquals(80, stat.getDistinctValuesCount(), 0.001);
            assertEquals(0.2, stat.getNullsFraction(), 0.001);
            assertEquals(-1, stat.getCollectionSize(), 0.001);
        }
        assertEquals(ColumnStatistic.unknown(), unknownStatistic);

    }

    @Test
    public void testCelonisArrayFirst() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_FIRST, Type.ARRAY_INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        assertEquals(4, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);

        assertEquals(4, emptyCollectionStatistics.getAverageRowSize(), 0.001);
        assertEquals(-1, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);

    }

    @Test
    public void testCelonisArrayLast() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_LAST, Type.ARRAY_INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        assertEquals(4, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);

        assertEquals(4, emptyCollectionStatistics.getAverageRowSize(), 0.001);
        assertEquals(-1, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);
    }

    @Test
    public void testCelonisArrayLag() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_LAG, Type.ARRAY_INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator, unaryTestScenario.intColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        for (var stat : List.of(columnStatistic, emptyCollectionStatistics)) {
            assertEquals(POSITIVE_INFINITY, stat.getMaxValue(), 0.001);
            assertEquals(NEGATIVE_INFINITY, stat.getMinValue(), 0.001);
            assertEquals(80, stat.getDistinctValuesCount(), 0.001);
            assertEquals(0.359, stat.getNullsFraction(), 0.001);
            assertEquals(40, stat.getAverageRowSize(), 0.001);
        }
        assertEquals(10, columnStatistic.getCollectionSize(), 0.001);
        assertEquals(0, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);
    }

    @Test
    public void testCelonisArrayLead() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_LEAD, Type.ARRAY_INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator, unaryTestScenario.intColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        for (var stat : List.of(columnStatistic, emptyCollectionStatistics)) {
            assertEquals(POSITIVE_INFINITY, stat.getMaxValue(), 0.001);
            assertEquals(NEGATIVE_INFINITY, stat.getMinValue(), 0.001);
            assertEquals(80, stat.getDistinctValuesCount(), 0.001);
            assertEquals(0.359, stat.getNullsFraction(), 0.001);
            assertEquals(40, stat.getAverageRowSize(), 0.001);
        }
        assertEquals(10, columnStatistic.getCollectionSize(), 0.001);
        assertEquals(0, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);
    }

    @Test
    public void testCelonisArrayTrimmedMean() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ARRAY_TRIMMED_MEAN, Type.INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        final var unknownStatistic = ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.unknownStatistics);
        final var emptyCollectionStatistics =
                ExpressionStatisticCalculator.calculate(callOperator, unaryTestScenario.emptyCollectionStatistics);

        // THEN
        assertEquals(ScalarType.DOUBLE.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);

        assertEquals(ScalarType.DOUBLE.getTypeSize(), emptyCollectionStatistics.getAverageRowSize(), 0.001);
        assertEquals(-1, emptyCollectionStatistics.getCollectionSize(), 0.001);

        assertEquals(ColumnStatistic.unknown(), unknownStatistic);
    }

    @Test
    public void testCelonisEncodeString() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_ENCODE_STRING, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.intColumnRefOperator, unaryTestScenario.stringArrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(79, columnStatistic.getMaxValue(), 0.001);
        assertEquals(-1, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.359, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);
    }

    @Test
    public void testCelonisDecodeString() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_DECODE_STRING, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.intColumnRefOperator, unaryTestScenario.stringArrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.359, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);
    }

    private static void testCelonisPatindex(CallOperator callOperator, Statistics statistics) {
        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(0, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.359, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(8, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisPatindexWithTwoArguments() {
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_PATINDEX, Type.BIGINT,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator, unaryTestScenario.stringColumnRefOperator));

        testCelonisPatindex(callOperator, statistics);
    }

    @Test
    public void testCelonisPatindexWithThreeArguments() {
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_PATINDEX, Type.BIGINT,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator, unaryTestScenario.stringColumnRefOperator,
                        unaryTestScenario.intColumnRefOperator));

        testCelonisPatindex(callOperator, statistics);
    }

    @Test
    public void testCelonisCalculateRangeEnd() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_CALCULATE_RANGE_END, Type.ARRAY_INT,
                Lists.newArrayList(multiaryTestScenario.datetimeColumnRefOperator, new ConstantOperator("1Y", Type.VARCHAR),
                        new ConstantOperator(10, Type.INT)));

        // WHEN
        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(1.4490589536E12, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisCalculateCrop() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_CALC_CROP, Type.ARRAY_BIGINT,
                Lists.newArrayList(multiaryTestScenario.stringArrayColumnRefOperator, new ConstantOperator("A", Type.VARCHAR),
                        new ConstantOperator("FIRST", Type.VARCHAR), new ConstantOperator("C", Type.VARCHAR),
                        new ConstantOperator("LAST", Type.VARCHAR)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(40, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisCalculateCropToNull() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_CALC_CROP_TO_NULL, Type.ARRAY_BIGINT,
                Lists.newArrayList(multiaryTestScenario.stringArrayColumnRefOperator, new ConstantOperator("A", Type.VARCHAR),
                        new ConstantOperator("FIRST", Type.VARCHAR), new ConstantOperator("C", Type.VARCHAR),
                        new ConstantOperator("LAST", Type.VARCHAR)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(40, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisStringSplit() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_STRING_SPLIT, Type.VARCHAR,
                Lists.newArrayList(multiaryTestScenario.stringColumnRefOperator, new ConstantOperator(" ", Type.VARCHAR),
                        new ConstantOperator(5, Type.INT)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisTranslate() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_TRANSLATE, Type.VARCHAR,
                Lists.newArrayList(multiaryTestScenario.stringColumnRefOperator, new ConstantOperator("-", Type.VARCHAR),
                        new ConstantOperator("_", Type.VARCHAR)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisMergeSortedArrays() {
        // GIVEN
        final var multiaryTestScenario = multiaryTestScenario();
        final var statistics = multiaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_MERGE_SORTED_ARRAYS, Type.ARRAY_VARCHAR,
                Lists.newArrayList(multiaryTestScenario.stringColumnRefOperator,
                        multiaryTestScenario.dateTimeArrayColumnRefOperator,
                        multiaryTestScenario.intArrayColumnRefOperator, multiaryTestScenario.intArrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisStringToInt() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_STRING_TO_INT, Type.BIGINT,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.BIGINT.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisStringToDouble() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_STRING_TO_DOUBLE, Type.DOUBLE,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.DOUBLE.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisToDouble() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_TO_DOUBLE, Type.DOUBLE,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.DOUBLE.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);
    }

    @Test
    public void testCelonisSanitizeInvalidUtf8() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_SANITIZE_INVALID_UTF8, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
    }

    @Test
    public void testCelonisStringHash() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_STRINGHASH, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
    }

    @Test
    public void testCelonisDedupSortedBy() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_DEDUP_SORTED_BY, Type.ARRAY_INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator, unaryTestScenario.arrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.359, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(40, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(10, columnStatistic.getCollectionSize(), 0.001);
    }

    @Test
    public void testCelonisLtrim() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_LTRIM, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator, new ConstantOperator(" ", Type.VARCHAR)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);
    }

    @Test
    public void testCelonisRtrim() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_RTRIM, Type.VARCHAR,
                Lists.newArrayList(unaryTestScenario.stringColumnRefOperator, new ConstantOperator(" ", Type.VARCHAR)));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(10, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);
    }

    private static void testCelonisRemapValuesUsingFunction(String function) {
        ColumnRefOperator col = new ColumnRefOperator(0, Type.INT, "c", true);
        Statistics statistics = Statistics.builder()
                .addColumnStatistic(col,
                        ColumnStatistic.builder()
                                .setMinValue(100)
                                .setMaxValue(1000)
                                .setNullsFraction(0.2)
                                .setDistinctValuesCount(100)
                                .build())
                .setOutputRowCount(10000).build();

        ConstantOperator const1 = new ConstantOperator(100, Type.INT);
        ConstantOperator const2 = new ConstantOperator(200, Type.INT);
        ConstantOperator const3 = new ConstantOperator(10, Type.INT);
        ConstantOperator const4 = new ConstantOperator(1010, Type.INT);
        ConstantOperator const5 = new ConstantOperator(900, Type.INT);
        ConstantOperator nullConst = ConstantOperator.createNull(Type.INT);

        // Remap values.
        CallOperator remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4))
                ));

        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with default mapping.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4)),
                        const5
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(3, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with duplicates.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2, const1)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4, const5))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(100, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with duplicates and default mapping.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2, const1)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4, const5)),
                        const5
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(900, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(2, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on the mapped to side.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, nullConst))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getMaxValue(), 0.001);
        assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.208, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on the mapped from side.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, nullConst)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on both sides.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, nullConst)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, nullConst))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getMaxValue(), 0.001);
        assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL default value.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4)),
                        nullConst
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        assertEquals(10, columnStatistic.getMinValue(), 0.001);
        assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        assertEquals(2, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.984, columnStatistic.getNullsFraction(), 0.001);
    }

    @Test
    public void testCelonisRemapValues() {
        testCelonisRemapValuesUsingFunction(FunctionSet.CELONIS_REMAP_VALUES);
    }

    @Test
    public void testCelonisRemapValuesConst() {
        testCelonisRemapValuesUsingFunction(FunctionSet.CELONIS_REMAP_VALUES_CONST);
    }

    private static class NullSkewTestScenario {
        private final ColumnRefOperator stringColumnRefOperator = new ColumnRefOperator(0, Type.VARCHAR, "str", true);
        private final Statistics statistics;

        public NullSkewTestScenario() {
            final var builder = Statistics.builder();
            statistics = builder.addColumnStatistic(stringColumnRefOperator, //
                            ColumnStatistic.builder() //
                                    .setMinValue(NEGATIVE_INFINITY) //
                                    .setMaxValue(POSITIVE_INFINITY)
                                    .setDistinctValuesCount(1000) //
                                    .setNullsFraction(0.8) //
                                    .setAverageRowSize(10) //
                                    .build()) //
                    .setOutputRowCount(1000) //
                    .build();

        }

    }

    private static class DataSkewTestScenario {
        private final ColumnRefOperator stringColumnRefOperator = new ColumnRefOperator(0, Type.VARCHAR, "str", true);
        private final Statistics statistics;

        public DataSkewTestScenario() {
            final var builder = Statistics.builder();
            statistics = builder.addColumnStatistic(stringColumnRefOperator, //
                            ColumnStatistic.builder() //
                                    .setMinValue(NEGATIVE_INFINITY) //
                                    .setMaxValue(POSITIVE_INFINITY)
                                    .setDistinctValuesCount(1000) //
                                    .setNullsFraction(0.1) //
                                    .setAverageRowSize(10) //
                                    .setHistogram(new Histogram(List.of(), Map.of("skewString1", 500L, "skewString2", 400L)))
                                    .build()) //
                    .setOutputRowCount(1000) //
                    .build();

        }

    }

    @Test
    public void testCelonisHashMcvProjectionWithNullSkew() {
        // GIVEN
        final var context = UtFrameUtils.createDefaultCtx();
        context.getSessionVariable().setEnableCelonisHashMcvs(true);

        final var nullSkewTestScenario = new NullSkewTestScenario();

        // WHEN
        CallOperator callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3,
                Type.VARCHAR, Lists.newArrayList(nullSkewTestScenario.stringColumnRefOperator));
        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, nullSkewTestScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());
        assertEquals(Map.of(new XXHASH3128V3().computeNull().toString(), 800L), columnStatistic.getHistogram().getMCV());

        // WHEN
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V4,
                Type.VARCHAR, Lists.newArrayList(nullSkewTestScenario.stringColumnRefOperator));
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, nullSkewTestScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());
        assertEquals(Map.of(new XXHASH3128V4().computeNull().toString(), 800L), columnStatistic.getHistogram().getMCV());
    }

    @Test
    public void testCelonisHashMcvProjectionWithDataSkew() {
        // GIVEN
        final var context = UtFrameUtils.createDefaultCtx();
        context.getSessionVariable().setEnableCelonisHashMcvs(true);

        final var dataSkewTestScenario = new DataSkewTestScenario();

        // WHEN
        CallOperator callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3,
                Type.VARCHAR, Lists.newArrayList(dataSkewTestScenario.stringColumnRefOperator));
        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, dataSkewTestScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());

        CelonisHashFunction hasher = new XXHASH3128V3();
        assertThat(columnStatistic.getHistogram().getMCV())
                .containsExactlyInAnyOrderEntriesOf(Map.of(
                        hasher.compute("skewString1").toString(), 500L, //
                        hasher.compute("skewString2").toString(), 400L, //
                        hasher.computeNull().toString(), 100L //
                ));

        // WHEN
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V4,
                Type.VARCHAR, Lists.newArrayList(dataSkewTestScenario.stringColumnRefOperator));
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, dataSkewTestScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());
        hasher = new XXHASH3128V4();
        assertThat(columnStatistic.getHistogram().getMCV())
                .containsExactlyInAnyOrderEntriesOf(Map.of(
                        hasher.compute("skewString1").toString(), 500L, //
                        hasher.compute("skewString2").toString(), 400L, //
                        hasher.computeNull().toString(), 100L //
                ));

        // WHEN
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE,
                Type.VARCHAR, Lists.newArrayList(dataSkewTestScenario.stringColumnRefOperator));
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, dataSkewTestScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(1000, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());
        hasher = new XXHASH3128NULLABLE();
        assertThat(columnStatistic.getHistogram().getMCV())
                .containsExactlyInAnyOrderEntriesOf(Map.of(
                        hasher.compute("skewString1").toString(), 500L, //
                        hasher.compute("skewString2").toString(), 400L //
                ));
    }
}
