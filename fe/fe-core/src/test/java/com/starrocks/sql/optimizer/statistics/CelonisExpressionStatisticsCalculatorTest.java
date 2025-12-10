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
import com.starrocks.catalog.Type;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

public class CelonisExpressionStatisticsCalculatorTest {
    @Test
    public void testCelonisUnaryFunctionCall() {
        ColumnRefOperator columnRefOperator = new ColumnRefOperator(0, Type.INT, "id", true);

        Statistics.Builder builder = Statistics.builder();
        double min = 0.0;
        double max = 100.0;
        double distinctValue = 80;
        double nullsFraction = 0.2;
        Statistics statistics = builder.addColumnStatistic(columnRefOperator,
                        ColumnStatistic.builder().setMinValue(min).setMaxValue(max).
                                setDistinctValuesCount(distinctValue).setNullsFraction(nullsFraction).setAverageRowSize(10).
                                build())
                .setOutputRowCount(100).build();
        // test CELONIS_XX_HASH3_128_V3 function
        CallOperator callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_V3,
                Type.INT, Lists.newArrayList(columnRefOperator));
        ColumnStatistic columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        Assertions.assertEquals(1.7014118346046923E38, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(-1.7014118346046923E38, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);
        // test CELONIS_XX_HASH3_128_NULLABLE function
        callOperator = new CallOperator(FunctionSet.CELONIS_XX_HASH3_128_NULLABLE, Type.INT,
                Lists.newArrayList(columnRefOperator));
        columnStatistic = ExpressionStatisticCalculator.calculate(callOperator, statistics);
        Assertions.assertEquals(1.7014118346046923E38, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(-1.7014118346046923E38, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(80, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);
    }

    @ParameterizedTest
    @ValueSource(strings = { FunctionSet.CELONIS_REMAP_VALUES, FunctionSet.CELONIS_REMAP_VALUES_CONST })
    public void testCelonisRemapValuesFunctionCall(String function) {
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
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with default mapping.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4)),
                        const5
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(3, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with duplicates.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2, const1)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4, const5))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(100, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with duplicates and default mapping.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2, const1)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4, const5)),
                        const5
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(900, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(2, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on the mapped to side.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, nullConst))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1000, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.208, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on the mapped from side.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, nullConst)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL on both sides.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, nullConst)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, nullConst))
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1000, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(100, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.2, columnStatistic.getNullsFraction(), 0.001);

        // Remap values with NULL default value.
        remapValuesCall = new CallOperator(function, Type.INT,
                Lists.newArrayList(
                        col,
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const1, const2)),
                        new ArrayOperator(Type.INT, true, Lists.newArrayList(const3, const4)),
                        nullConst
                ));

        columnStatistic = ExpressionStatisticCalculator.calculate(remapValuesCall, statistics);
        Assertions.assertEquals(10, columnStatistic.getMinValue(), 0.001);
        Assertions.assertEquals(1010, columnStatistic.getMaxValue(), 0.001);
        Assertions.assertEquals(2, columnStatistic.getDistinctValuesCount(), 0.001);
        Assertions.assertEquals(0.984, columnStatistic.getNullsFraction(), 0.001);
    }
}
