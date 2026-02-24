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

import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;

import java.util.List;
import java.util.stream.Collectors;

public class CelonisHistogramStatisticsUtils {

    /**
     * Estimates statistics for CELONIS_IN predicate with histogram support.
     * Unlike the standard IN predicate in SR, NULL constants in the match list will match NULL values.
     */
    public static Statistics estimateCelonisInPredicateWithHistogram(ColumnRefOperator columnRefOperator,
                                                                     ColumnStatistic columnStatistic,
                                                                     List<ConstantOperator> constants,
                                                                     Statistics statistics) {
        final var nonNullConstants = constants.stream() //
                .filter(constant -> !constant.isNull()) //
                .collect(Collectors.toList());

        // No NULL match: Use SR logic
        if (nonNullConstants.size() == constants.size()) {
            return HistogramStatisticsUtils.estimateInPredicateWithHistogram(
                    columnRefOperator, columnStatistic, constants, false, statistics);
        }

        double inputRowCount = statistics.getOutputRowCount();
        double nullCount = Math.max(0, Math.min(inputRowCount, inputRowCount * columnStatistic.getNullsFraction()));

        if (nonNullConstants.isEmpty()) {
            // Only NULLs in match
            final var newStat = ColumnStatistic.buildFrom(columnStatistic) //
                    .setNullsFraction(nullCount > 0 ? 1.0 : 0.0) //
                    .setDistinctValuesCount(0) //
                    .setHistogram(null) //
                    .build();
            return StatisticsEstimateUtils.adjustStatisticsByRowCount(
                    Statistics.buildFrom(statistics) //
                            .setOutputRowCount(nullCount) //
                            .addColumnStatistic(columnRefOperator, newStat) //
                            .build(), //
                    nullCount);
        }

        // Non-null + NULL constants branch
        // First calculate stats for the non-null constants
        final var estimatedStats = HistogramStatisticsUtils.estimateInPredicateWithHistogram(
                columnRefOperator, columnStatistic, nonNullConstants, false, statistics);

        // Adjust the row count and null fractions based on the null count
        double totalRowCount = Math.min(inputRowCount, estimatedStats.getOutputRowCount() + nullCount);
        double nullsFraction = totalRowCount > 0 ? (nullCount / totalRowCount) : 0.0;

        final var estimatedColumnStats = ColumnStatistic.buildFrom(estimatedStats.getColumnStatistic(columnRefOperator)) //
                .setNullsFraction(nullsFraction) //
                .build();

        return StatisticsEstimateUtils.adjustStatisticsByRowCount(
                Statistics.buildFrom(estimatedStats) //
                        .setOutputRowCount(totalRowCount) //
                        .addColumnStatistic(columnRefOperator, estimatedColumnStats) //
                        .build(), //
                totalRowCount);
    }
}
