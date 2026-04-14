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

    private static class BinaryTestScenario {
        private final ColumnRefOperator intColumnRefOperator;
        private final ColumnRefOperator arrayColumnRefOperator;
        private final ColumnRefOperator stringColumnRefOperator;
        private final ColumnRefOperator stringArrayColumnRefOperator;
        private final Statistics statistics;

        public BinaryTestScenario(ColumnRefOperator intColumnRefOperator, ColumnRefOperator arrayColumnRefOperator,
                                  ColumnRefOperator stringColumnRefOperator,
                                  ColumnRefOperator stringArrayColumnRefOperator,
                                  Statistics statistics) {
            this.intColumnRefOperator = intColumnRefOperator;
            this.arrayColumnRefOperator = arrayColumnRefOperator;
            this.stringColumnRefOperator = stringColumnRefOperator;
            this.stringArrayColumnRefOperator = stringArrayColumnRefOperator;
            this.statistics = statistics;
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

    private static BinaryTestScenario binaryTestScenario() {
        ColumnRefOperator arrayColumnRefOperator = new ColumnRefOperator(0, Type.ARRAY_INT, "array", true);
        ColumnRefOperator intColumnRefOperator = new ColumnRefOperator(1, Type.INT, "int", true);
        ColumnRefOperator stringColumnRefOperator = new ColumnRefOperator(2, Type.VARCHAR, "string", true);
        ColumnRefOperator stringArrayColumnRefOperator = new ColumnRefOperator(3, Type.ARRAY_VARCHAR, "stringArray", true);
        Statistics.Builder builder = Statistics.builder();
        Statistics statistics = Statistics.builder() //
                .addColumnStatistic(arrayColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(40) //
                                .setCollectionSize(10) //
                                .build()) //
                .addColumnStatistic(intColumnRefOperator, //
                        ColumnStatistic.builder() //
                                .setMinValue(0.0) //
                                .setMaxValue(100.0) //
                                .setDistinctValuesCount(80) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(10) //
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

        return new BinaryTestScenario(intColumnRefOperator, arrayColumnRefOperator, stringColumnRefOperator,
                stringArrayColumnRefOperator, statistics);
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

    @Test
    public void testCelonisPeekMergedSortedArrays() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_PEEK_MERGED_SORTED_ARRAYS, Type.INT,
                Lists.newArrayList(unaryTestScenario.arrayColumnRefOperator, unaryTestScenario.stringArrayColumnRefOperator,
                        unaryTestScenario.stringArrayColumnRefOperator, unaryTestScenario.stringArrayColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(4, columnStatistic.getAverageRowSize(), 0.001);
        assertEquals(-1, columnStatistic.getCollectionSize(), 0.001);
    }

    @Test
    public void testCelonisQNorm() {
        // GIVEN
        final var unaryTestScenario = unaryTestScenario();
        final var statistics = unaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_QNORM, Type.DOUBLE,
                Lists.newArrayList(unaryTestScenario.intColumnRefOperator));

        // WHEN
        var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // WHEN
        // All rows have a valid normal distribution input.
        var newColumnStat = ColumnStatistic.buildFrom(statistics.getColumnStatistic(unaryTestScenario.intColumnRefOperator)) //
                .setMinValue(0.1) //
                .setMaxValue(0.8) //
                .build();
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, Statistics.builder() //
                .setOutputRowCount(1337) //
                .addColumnStatistic(unaryTestScenario.intColumnRefOperator, newColumnStat) //
                .build());

        // THEN
        assertEquals(-1.2815, columnStatistic.getMinValue(), 0.001);
        assertEquals(0.8416212335729144, columnStatistic.getMaxValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // WHEN
        // All rows must have invalid normal distribution input.
        newColumnStat = ColumnStatistic.buildFrom(statistics.getColumnStatistic(unaryTestScenario.intColumnRefOperator)) //
                .setMinValue(NEGATIVE_INFINITY) //
                .setMaxValue(-0.1) //
                .build();
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, Statistics.builder()
                .setOutputRowCount(1337) //
                .addColumnStatistic(unaryTestScenario.intColumnRefOperator, newColumnStat) //
                .build());

        // THEN
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(1, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(1.0, columnStatistic.getNullsFraction(), 0.001);
    }

    @Test
    public void testCelonisStringArrayJoin() {
        // GIVEN
        final var binaryTestScenario = binaryTestScenario();
        final var statistics = binaryTestScenario.statistics;
        final var callOperator = new CallOperator(FunctionSet.CELONIS_STRING_ARRAY_JOIN, Type.VARCHAR,
                Lists.newArrayList(binaryTestScenario.stringArrayColumnRefOperator, binaryTestScenario.stringColumnRefOperator));

        // WHEN
        final var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);

        // THEN
        assertEquals(POSITIVE_INFINITY, columnStatistic.getMaxValue(), 0.001);
        assertEquals(NEGATIVE_INFINITY, columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.359, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(140, columnStatistic.getAverageRowSize(), 0.001);
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

        // GIVEN
        // Remap values MCV propagation with non-null default value.
        final var stringCol = new ColumnRefOperator(1, Type.VARCHAR, "str", true);
        final var stringStatistics = Statistics.builder()
                .addColumnStatistic(stringCol,
                        ColumnStatistic.builder() //
                                .setMinValue(NEGATIVE_INFINITY) //
                                .setMaxValue(POSITIVE_INFINITY) //
                                .setNullsFraction(0.1) //
                                .setDistinctValuesCount(100) //
                                .setAverageRowSize(Type.VARCHAR.getTypeSize()) //
                                .setHistogram(new Histogram(List.of(), //
                                        Map.of("manuell", 300L, //
                                        "maschinell", 200L, //
                                        "orangensaft", 30L, //
                                        "legacy", 150L))) //
                                .build()) //
                .setOutputRowCount(1000) //
                .build();

        final var manuell = ConstantOperator.createVarchar("manuell");
        final var maschinell = ConstantOperator.createVarchar("maschinell");
        final var orangensaft = ConstantOperator.createVarchar("orangensaft");
        final var other = ConstantOperator.createVarchar("other");
        final var nullVarchar = ConstantOperator.createNull(Type.VARCHAR);

        remapValuesCall = new CallOperator(function, Type.VARCHAR,
                Lists.newArrayList(
                        stringCol,
                        new ArrayOperator(Type.VARCHAR, true, Lists.newArrayList(manuell, maschinell, orangensaft, nullVarchar)),
                        new ArrayOperator(Type.VARCHAR, true, Lists.newArrayList(manuell, maschinell, maschinell, nullVarchar)),
                        other
                ));

        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, stringStatistics);

        // THEN
        assertEquals(0.1, columnStatistic.getNullsFraction(), 0.001);
        assertNotNull(columnStatistic.getHistogram());
        assertThat(columnStatistic.getHistogram().getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                        "manuell", 300L, //
                        "maschinell", 200L + 30L, //
                        "other", 262L) // rest of row count inferred from MCVs
        );
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
    @Test
    public void testCelonisGreatestLeastFunctionCall() {
        ColumnRefOperator col1 = new ColumnRefOperator(1, Type.INT, "col1", true);
        ColumnRefOperator col2 = new ColumnRefOperator(2, Type.INT, "col2", true);
        ColumnRefOperator col3 = new ColumnRefOperator(3, Type.INT, "col3", true);

        Statistics statistics = Statistics.builder() //
                .addColumnStatistic(col1, //
                        ColumnStatistic.builder() //
                                .setMinValue(10.0) //
                                .setMaxValue(50.0) //
                                .setDistinctValuesCount(20) //
                                .setNullsFraction(0.1) //
                                .setAverageRowSize(4) //
                                .build()) //
                .addColumnStatistic(col2, //
                        ColumnStatistic.builder() //
                                .setMinValue(20.0) //
                                .setMaxValue(70.0) //
                                .setDistinctValuesCount(30) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(4) //
                                .build()) //
                .addColumnStatistic(col3, //
                        ColumnStatistic.builder() //
                                .setMinValue(5.0) //
                                .setMaxValue(90.0) //
                                .setDistinctValuesCount(40) //
                                .setNullsFraction(0.3) //
                                .setAverageRowSize(4) //
                                .build()) //
                .setOutputRowCount(1000.0) //
                .build();

        // with 1 Column
        CallOperator greatestCall = new CallOperator(FunctionSet.CELONIS_GREATEST, Type.INT,
                Lists.newArrayList(col1));
        ColumnStatistic greatestColumnStatistic = ExpressionStatisticCalculator.calculate(greatestCall, statistics);

        assertEquals(10.0, greatestColumnStatistic.getMinValue(), 0.001);
        assertEquals(50.0, greatestColumnStatistic.getMaxValue(), 0.001);
        assertEquals(20.0, greatestColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, greatestColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, greatestColumnStatistic.getAverageRowSize(), 0.001);

        CallOperator leastCall = new CallOperator(FunctionSet.CELONIS_LEAST, Type.INT,
                Lists.newArrayList(col1));
        ColumnStatistic leastColumnStatistic = ExpressionStatisticCalculator.calculate(leastCall, statistics);

        assertEquals(10.0, leastColumnStatistic.getMinValue(), 0.001);
        assertEquals(50.0, leastColumnStatistic.getMaxValue(), 0.001);
        assertEquals(20.0, leastColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, leastColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, leastColumnStatistic.getAverageRowSize(), 0.001);

        // with 2 Columns
        greatestCall = new CallOperator(FunctionSet.CELONIS_GREATEST, Type.INT, Lists.newArrayList(col1, col2));
        greatestColumnStatistic = ExpressionStatisticCalculator.calculate(greatestCall, statistics);

        assertEquals(20.0, greatestColumnStatistic.getMinValue(), 0.001);
        assertEquals(70.0, greatestColumnStatistic.getMaxValue(), 0.001);
        assertEquals(50.0, greatestColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, greatestColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, greatestColumnStatistic.getAverageRowSize(), 0.001);

        leastCall = new CallOperator(FunctionSet.CELONIS_LEAST, Type.INT, Lists.newArrayList(col1, col2));
        leastColumnStatistic = ExpressionStatisticCalculator.calculate(leastCall, statistics);

        assertEquals(10.0, leastColumnStatistic.getMinValue(), 0.001);
        assertEquals(50.0, leastColumnStatistic.getMaxValue(), 0.001);
        assertEquals(50.0, leastColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, leastColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, leastColumnStatistic.getAverageRowSize(), 0.001);

        // with 3 Columns
        greatestCall = new CallOperator(FunctionSet.CELONIS_GREATEST, Type.INT, Lists.newArrayList(col1, col2, col3));
        greatestColumnStatistic = ExpressionStatisticCalculator.calculate(greatestCall, statistics);

        assertEquals(20.0, greatestColumnStatistic.getMinValue(), 0.001);
        assertEquals(90.0, greatestColumnStatistic.getMaxValue(), 0.001);
        assertEquals(90.0, greatestColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, greatestColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, greatestColumnStatistic.getAverageRowSize(), 0.001);

        leastCall = new CallOperator(FunctionSet.CELONIS_LEAST, Type.INT, Lists.newArrayList(col1, col2, col3));
        leastColumnStatistic = ExpressionStatisticCalculator.calculate(leastCall, statistics);

        assertEquals(5.0, leastColumnStatistic.getMinValue(), 0.001);
        assertEquals(50.0, leastColumnStatistic.getMaxValue(), 0.001);
        assertEquals(90.0, leastColumnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.1, leastColumnStatistic.getNullsFraction(), 0.001);
        assertEquals(4, leastColumnStatistic.getAverageRowSize(), 0.001);

        // with string columns
        ColumnRefOperator stringCol1 = new ColumnRefOperator(10, Type.VARCHAR, "stringCol1", true);
        ColumnRefOperator stringCol2 = new ColumnRefOperator(11, Type.VARCHAR, "stringCol2", true);

        Statistics stringStatistics = Statistics.builder() //
                .addColumnStatistic(stringCol1, //
                        ColumnStatistic.builder() //
                                .setDistinctValuesCount(20) //
                                .setNullsFraction(0.1) //
                                .setAverageRowSize(4) //
                                .build()) //
                .addColumnStatistic(stringCol2, //
                        ColumnStatistic.builder() //
                                .setDistinctValuesCount(30) //
                                .setNullsFraction(0.2) //
                                .setAverageRowSize(100) //
                                .build()) //
                .setOutputRowCount(1000.0) //
                .build();

        greatestCall = new CallOperator(FunctionSet.CELONIS_GREATEST, Type.STRING, Lists.newArrayList(stringCol1, stringCol2));
        greatestColumnStatistic = ExpressionStatisticCalculator.calculate(greatestCall, stringStatistics);

        assertEquals(52, greatestColumnStatistic.getAverageRowSize(), 0.001);

        leastCall = new CallOperator(FunctionSet.CELONIS_LEAST, Type.STRING, Lists.newArrayList(stringCol1, stringCol2));
        leastColumnStatistic = ExpressionStatisticCalculator.calculate(leastCall, stringStatistics);

        assertEquals(52, leastColumnStatistic.getAverageRowSize(), 0.001);
    }



    private void testCelonisMultiArgHash(String functionName) {
        // GIVEN
        final var binaryScenario = binaryTestScenario();
        var callOperator = new CallOperator(functionName, Type.LARGEINT,
                Lists.newArrayList(binaryScenario.intColumnRefOperator, binaryScenario.stringColumnRefOperator));

        // WHEN
        var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, binaryScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.LARGEINT.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);

        // GIVEN
        final var multiaryScenario = multiaryTestScenario();
        callOperator = new CallOperator(functionName, Type.LARGEINT,
                Lists.newArrayList(multiaryScenario.stringColumnRefOperator,
                        multiaryScenario.datetimeColumnRefOperator,
                        multiaryScenario.stringArrayColumnRefOperator));

        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, multiaryScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.LARGEINT.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);

        // GIVEN
        final var col1 = new ColumnRefOperator(10, Type.BIGINT, "col1", true);
        final var col2 = new ColumnRefOperator(11, Type.BIGINT, "col2", true);
        final var col3 = new ColumnRefOperator(12, Type.BIGINT, "col3", true);
        final var stats = Statistics.builder()
                .addColumnStatistic(col1, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(100).setDistinctValuesCount(10)
                        .setNullsFraction(0.0).setAverageRowSize(8).build())
                .addColumnStatistic(col2, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(200).setDistinctValuesCount(50)
                        .setNullsFraction(0.0).setAverageRowSize(8).build())
                .addColumnStatistic(col3, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(300).setDistinctValuesCount(30)
                        .setNullsFraction(0.0).setAverageRowSize(8).build())
                .setOutputRowCount(1000)
                .build();

        callOperator = new CallOperator(functionName, Type.LARGEINT, Lists.newArrayList(col1, col2));
        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, stats);
        // THEN
        assertEquals(50, columnStatistic.getDistinctValuesCount(), 0.001);

        callOperator = new CallOperator(functionName, Type.LARGEINT, Lists.newArrayList(col1, col2, col3));
        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, stats);
        // THEN
        assertEquals(50, columnStatistic.getDistinctValuesCount(), 0.001);
    }

    @Test
    public void testCelonisMultiArgHashXxHash3128() {
        testCelonisMultiArgHash(FunctionSet.CELONIS_XX_HASH3_128);
    }

    @Test
    public void testCelonisMultiArgHashXxHash3128V2() {
        testCelonisMultiArgHash(FunctionSet.CELONIS_XX_HASH3_128_V2);
    }

    @Test
    public void testCelonisMultiArgHashXxHash3128V3() {
        testCelonisMultiArgHash(FunctionSet.CELONIS_XX_HASH3_128_V3);
    }

    @Test
    public void testCelonisMultiArgHashXxHash3128V4() {
        testCelonisMultiArgHash(FunctionSet.CELONIS_XX_HASH3_128_V4);
    }

    @Test
    public void testCelonisMultiArgHashXxHash3128Nullable() {
        // GIVEN
        final var binaryScenario = binaryTestScenario();
        var callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.LARGEINT,
                Lists.newArrayList(binaryScenario.intColumnRefOperator, binaryScenario.stringColumnRefOperator));

        // WHEN
        var columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, binaryScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        // nullable preserves combined null fraction: 1 - (1-0.2)*(1-0.2) = 0.36
        assertEquals(0.36, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.LARGEINT.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);

        // GIVEN
        final var multiaryScenario = multiaryTestScenario();
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.LARGEINT,
                Lists.newArrayList(multiaryScenario.stringColumnRefOperator,
                        multiaryScenario.datetimeColumnRefOperator,
                        multiaryScenario.stringArrayColumnRefOperator));

        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, multiaryScenario.statistics);

        // THEN
        assertEquals(LargeIntLiteral.LARGE_INT_MAX.doubleValue(), columnStatistic.getMaxValue(), 0.001);
        assertEquals(LargeIntLiteral.LARGE_INT_MIN.doubleValue(), columnStatistic.getMinValue(), 0.001);
        assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        // nullable preserves combined null fraction: 1 - (1-0.2)^3 = 0.488
        assertEquals(0.488, columnStatistic.getNullsFraction(), 0.001);
        assertEquals(Type.LARGEINT.getTypeSize(), columnStatistic.getAverageRowSize(), 0.001);

        // GIVEN
        // Test NDV = max(children NDVs) with different NDVs
        final var col1 = new ColumnRefOperator(10, Type.BIGINT, "col1", true);
        final var col2 = new ColumnRefOperator(11, Type.BIGINT, "col2", true);
        final var col3 = new ColumnRefOperator(12, Type.BIGINT, "col3", true);
        final var stats = Statistics.builder()
                .addColumnStatistic(col1, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(100).setDistinctValuesCount(10)
                        .setNullsFraction(0.1).setAverageRowSize(8).build())
                .addColumnStatistic(col2, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(200).setDistinctValuesCount(50)
                        .setNullsFraction(0.05).setAverageRowSize(8).build())
                .addColumnStatistic(col3, ColumnStatistic.builder()
                        .setMinValue(0).setMaxValue(300).setDistinctValuesCount(30)
                        .setNullsFraction(0.0).setAverageRowSize(8).build())
                .setOutputRowCount(1000)
                .build();

        // Binary: NDV = max(10, 50) = 50, nullsFraction = 1 - (1-0.1)*(1-0.05) = 0.145
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.LARGEINT,
                Lists.newArrayList(col1, col2));

        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, stats);

        // THEN
        assertEquals(50, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.145, columnStatistic.getNullsFraction(), 0.001);

        // GIVEN
        // Multiary: NDV = max(10, 50, 30) = 50, nullsFraction = 1 - (1-0.1)*(1-0.05)*(1-0.0) = 0.145
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.LARGEINT,
                Lists.newArrayList(col1, col2, col3));

        // WHEN
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, stats);

        // THEN
        assertEquals(50, columnStatistic.getDistinctValuesCount(), 0.001);
        assertEquals(0.145, columnStatistic.getNullsFraction(), 0.001);
    }
}
