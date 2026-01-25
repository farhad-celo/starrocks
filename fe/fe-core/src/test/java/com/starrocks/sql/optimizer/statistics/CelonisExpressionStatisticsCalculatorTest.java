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
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import org.junit.jupiter.api.Test;

import java.util.List;

import static java.lang.Double.NEGATIVE_INFINITY;
import static java.lang.Double.POSITIVE_INFINITY;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class CelonisExpressionStatisticsCalculatorTest {

    private static class UnaryTestScenario {
        private final ColumnRefOperator intColumnRefOperator;
        private final ColumnRefOperator arrayColumnRefOperator;
        private final Statistics statistics;
        private final Statistics unknownStatistics;
        private final Statistics emptyCollectionStatistics;

        public UnaryTestScenario(ColumnRefOperator intColumnRefOperator, ColumnRefOperator arrayColumnRefOperator,
                                 Statistics statistics) {
            this.intColumnRefOperator = intColumnRefOperator;
            this.arrayColumnRefOperator = arrayColumnRefOperator;
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
                .setOutputRowCount(100) //
                .build();

        return new UnaryTestScenario(intColumnRefOperator, arrayColumnRefOperator, statistics);
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

}
