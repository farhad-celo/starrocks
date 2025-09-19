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

import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CastOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;

import java.util.Optional;

public class CelonisPredicateStatisticsCalculator {
    public static Statistics estimateCall(CallOperator call, Statistics statistics) {
        if (call.getFnName().equalsIgnoreCase(FunctionSet.CELONIS_IN)) {
            return estimateCelonisIn(call, statistics);
        }
        return null;
    }

    private static ScalarOperator getChildForCastOperator(ScalarOperator operator) {
        while (operator instanceof CastOperator) {
            operator = operator.getChild(0);
        }
        return operator;
    }

    private static boolean hasConstantNullMatch(ScalarOperator operator) {
        if (operator instanceof ArrayOperator) {
            ArrayOperator arrayOperator = (ArrayOperator) operator;
            return arrayOperator.getChildren().stream().anyMatch(ScalarOperator::isConstantNull);
        }

        return false;
    }

    private static ColumnStatistic calculateMatchArrayStatistic(ScalarOperator operator, Statistics statistics) {
        if (operator instanceof ArrayOperator) {
            double minValue = Double.POSITIVE_INFINITY;
            double maxValue = Double.NEGATIVE_INFINITY;
            double distinctValues = 0;

            for (ScalarOperator child : operator.getChildren()) {
                if (child.isConstantNull()) {
                    continue;
                }

                ColumnStatistic childColumnStatistic = ExpressionStatisticCalculator.calculate(child, statistics);
                if (childColumnStatistic.isUnknown()) {
                    return ColumnStatistic.unknown();
                }

                minValue = Math.min(minValue, childColumnStatistic.getMinValue());
                maxValue = Math.max(maxValue, childColumnStatistic.getMaxValue());
                distinctValues += childColumnStatistic.getDistinctValuesCount();
            }

            return new ColumnStatistic(minValue, maxValue, 0, 0, distinctValues);
        }
        return ColumnStatistic.unknown();
    }

    /**
     * CELONIS_IN selectivity estimation is split into 2 parts:
     * - estimate the number of null values in the output: if there is a null constant in the list of matches, all nulls in
     * the input column qualify. The number of null values is computed based on the nulls fractions of the input
     * column. If there is no null constant in the match list, the number of null values is 0.
     * number of input rows * nulls fraction
     * - estimate the number of non-null values in the output: the number of non-null values is estimate by assuming uniform
     * distribution, similar to the SEQ IN operator, adjusted to exclude nulls.
     * number of input rows * (1 - nulls fraction) * ( number of distinct matches / number of distinct input values )
     */
    private static Statistics estimateCelonisIn(CallOperator call, Statistics statistics) {
        ScalarOperator firstChild = getChildForCastOperator(call.getChild(0));
        ScalarOperator secondChild = getChildForCastOperator(call.getChild(1));


        // 1. compute CELONIS_IN children column statistics
        ColumnStatistic inColumnStatistic = ExpressionStatisticCalculator.calculate(firstChild, statistics);
        ColumnStatistic inMatchStatistic = calculateMatchArrayStatistic(secondChild, statistics);

        // 2. compute CELONIS_IN null matches
        boolean hasNullMatch = hasConstantNullMatch(secondChild);
        double nullCount = Math.min(statistics.getOutputRowCount() * inColumnStatistic.getNullsFraction(),
                statistics.getOutputRowCount());

        double columnDistinctValues = Math.max(inColumnStatistic.getDistinctValuesCount(), 1);
        double matchDistinctValues = Math.max(inMatchStatistic.getDistinctValuesCount(), 1);

        double columnMaxVal = inColumnStatistic.getMaxValue();
        double columnMinVal = inColumnStatistic.getMinValue();

        double matchMaxVal = inMatchStatistic.getMaxValue();
        double matchMinVal = inMatchStatistic.getMinValue();

        // assume string column always has an overlap
        boolean hasOverlap = firstChild.getType().getPrimitiveType().isCharFamily() ||
                Math.max(columnMinVal, matchMinVal) <= Math.min(columnMaxVal, matchMaxVal);

        // 3. compute CELONIS_IN selectivity
        double selectivity;
        if (inColumnStatistic.isUnknown() || inColumnStatistic.hasNaNValue() ||
                inMatchStatistic.isUnknown() || inMatchStatistic.hasNaNValue() ||
                !(firstChild.isColumnRef())) {
            // use default selectivity if column statistic is unknown or has NaN values.
            // can not get accurate column statistics if it is not ColumnRef operator
            selectivity = StatisticsEstimateCoefficient.IN_PREDICATE_DEFAULT_FILTER_COEFFICIENT;
        } else {
            // children column statistics are not unknown.
            selectivity = hasOverlap ? Math.min(1.0, matchDistinctValues / columnDistinctValues) : 0.0;
        }

        double nonNullCount = statistics.getOutputRowCount() - nullCount;
        nonNullCount = Math.min(nonNullCount * selectivity, nonNullCount);
        double rowCount = hasNullMatch ? nullCount + nonNullCount : nonNullCount;

        // 4. compute the CELONIS_IN first child column statistics after predicate
        double columnNullsFraction = hasNullMatch ? (nullCount / rowCount) : 0.0;
        if (!inMatchStatistic.isUnknown() && !inMatchStatistic.hasNaNValue() && hasOverlap) {
            columnMaxVal = Math.min(columnMaxVal, matchMaxVal);
            columnMinVal = Math.max(columnMinVal, matchMinVal);
            columnDistinctValues = Math.min(columnDistinctValues, matchDistinctValues);
        }

        ColumnStatistic newInColumnStatistic =
                ColumnStatistic.buildFrom(inColumnStatistic)
                        .setMinValue(columnMinVal)
                        .setMaxValue(columnMaxVal)
                        .setDistinctValuesCount(columnDistinctValues)
                        .setNullsFraction(columnNullsFraction)
                        .setAverageRowSize(inColumnStatistic.getAverageRowSize())
                        .build();

        // only columnRefOperator could add column statistic to statistics
        Optional<ColumnRefOperator> childOpt =
                firstChild.isColumnRef() ? Optional.of((ColumnRefOperator) firstChild) : Optional.empty();

        Statistics inStatistics = childOpt.map(operator ->
                        Statistics.buildFrom(statistics).setOutputRowCount(rowCount).
                                addColumnStatistic(operator, newInColumnStatistic).build()).
                orElseGet(() -> Statistics.buildFrom(statistics).setOutputRowCount(rowCount).build());
        return StatisticsEstimateUtils.adjustStatisticsByRowCount(inStatistics, rowCount);
    }
}
