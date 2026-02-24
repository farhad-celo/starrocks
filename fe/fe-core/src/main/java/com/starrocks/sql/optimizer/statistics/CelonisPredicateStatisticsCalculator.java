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
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

import static com.starrocks.sql.optimizer.statistics.CelonisHistogramStatisticsUtils.estimateCelonisInPredicateWithHistogram;

public class CelonisPredicateStatisticsCalculator {
    public static Statistics estimateCall(CallOperator call, Statistics statistics) {
        switch (call.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_IN:
                return estimateCelonisIn(call, statistics);
            case FunctionSet.CELONIS_MULTI_IN:
                return estimateCelonisMultiIn(call, statistics);
            default:
                return null;
        }
    }

    private static ScalarOperator getChildForCastOperator(ScalarOperator operator) {
        while (operator instanceof CastOperator) {
            operator = operator.getChild(0);
        }
        return operator;
    }

    private static boolean hasConstantNullMatch(List<ScalarOperator> matchChildrenList) {
        return matchChildrenList.stream().anyMatch(ScalarOperator::isConstantNull);
    }

    private static ColumnStatistic calculateMatchArrayStatistic(List<ScalarOperator> matches, Statistics statistics) {
        double minValue = Double.POSITIVE_INFINITY;
        double maxValue = Double.NEGATIVE_INFINITY;
        double distinctValues = 0;

        for (ScalarOperator match : matches) {
            if (match.isConstantNull()) {
                continue;
            }

            ColumnStatistic childColumnStatistic = ExpressionStatisticCalculator.calculate(match, statistics);
            if (childColumnStatistic.isUnknown()) {
                return ColumnStatistic.unknown();
            }

            minValue = Math.min(minValue, childColumnStatistic.getMinValue());
            maxValue = Math.max(maxValue, childColumnStatistic.getMaxValue());
            distinctValues += childColumnStatistic.getDistinctValuesCount();
            // NULL values originating from columns in the match list are ignored.
        }

        return new ColumnStatistic(minValue, maxValue, 0, 0, distinctValues);
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

        List<ScalarOperator> matchChildrenList = new ArrayList<>();
        if (secondChild instanceof ArrayOperator) {
            matchChildrenList = secondChild.getChildren().stream() //
                    .map(CelonisPredicateStatisticsCalculator::getChildForCastOperator) //
                    .distinct() //
                    .collect(Collectors.toList());

            if (matchChildrenList.isEmpty()) {
                // If the match list is empty the predicate will always evaluate to false.
                return Statistics.buildFrom(statistics).setOutputRowCount(0).build();
            }

            final boolean isArgumentColumnRef = firstChild.isColumnRef();
            final boolean allConstants = matchChildrenList.stream().allMatch(op -> op instanceof ConstantOperator);
            if (isArgumentColumnRef && allConstants && !inColumnStatistic.isUnknown() &&
                    inColumnStatistic.getHistogram() != null) {
                return estimateCelonisInPredicateWithHistogram(
                        (ColumnRefOperator) firstChild,
                        inColumnStatistic,
                        matchChildrenList.stream()
                                .map(op -> (ConstantOperator) op)
                                .collect(Collectors.toList()),
                        statistics
                );
            }
        }

        ColumnStatistic inMatchStatistic = matchChildrenList.isEmpty() ? ColumnStatistic.unknown() :
                calculateMatchArrayStatistic(matchChildrenList, statistics);

        // 2. compute CELONIS_IN null matches
        boolean hasNullMatch = hasConstantNullMatch(matchChildrenList);
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

    // Extracts the arguments from the input CallOperator. The supported CallOperators are named_struct and row.
    // Input: named_struct ( "f1" , inputOperator1 , "f2" , inputOperator2 )
    // Input: row ( inputOperator1 , inputOperator2 )
    // Output: [ inputOperator1 , inputOperator2 ]
    private static List<ScalarOperator> extractInputs(ScalarOperator operator) {
        if (!(operator instanceof CallOperator)) {
            return null;
        }

        switch (((CallOperator) operator).getFnName()) {
            case FunctionSet.NAMED_STRUCT:
                return IntStream.range(0, operator.getChildren().size())
                        .filter(i -> i % 2 != 0)
                        .mapToObj(operator::getChild)
                        .collect(Collectors.toList());
            case FunctionSet.ROW:
                return operator.getChildren();
            default:
                return null;
        }
    }

    // Transforms the match lists for each column to a list of match tuples.
    // Input: [ [ constantOperator1 , constantOperator2 ] , [ constantOperator3 , constantOperator4 ] ]
    // Output: [ [ constantOperator1 , constantOperator3 ] , [ constantOperator2 , constantOperator4 ] ]
    private static List<List<ScalarOperator>> transformToMatchTuples(List<ScalarOperator> inputMatches) {
        int tupleSize = inputMatches.size();
        ScalarOperator firstChildOperator = inputMatches.get(0);
        if (!(firstChildOperator instanceof ArrayOperator)) {
            return null;
        }
        int tupleCount = firstChildOperator.getChildren().size();

        List<List<ScalarOperator>> matchTuples = new ArrayList<>();
        for (int i = 0; i < tupleCount; ++i) {
            List<ScalarOperator> matchTuple = new ArrayList<>();
            for (int j = 0; j < tupleSize; ++j) {
                ScalarOperator childOperator = inputMatches.get(j);
                if (!(childOperator instanceof ArrayOperator && childOperator.getChildren().size() == tupleCount)) {
                    return null;
                }
                matchTuple.add(childOperator.getChild(i));
            }
            matchTuples.add(matchTuple);
        }

        return matchTuples;
    }

    // Checks if the tuple of matches falls in the ranges of the input columns, otherwise the selectivity will be 0.
    private static boolean isOverlappingMatchTuple(List<ScalarOperator> multiInMatchTuple,
                                                   List<ColumnStatistic> multiInMatchTupleStatistics,
                                                   List<ScalarOperator> multiInColumns,
                                                   List<ColumnStatistic> multiInColumnsStatistics) {
        return IntStream.range(0, multiInMatchTuple.size()).allMatch(i -> {
            ColumnStatistic multiInColumnStatistic = multiInColumnsStatistics.get(i);
            ColumnStatistic multiInMatchStatistic = multiInMatchTupleStatistics.get(i);
            if (multiInColumnStatistic.isUnknown() || multiInMatchStatistic.isUnknown() ||
                    multiInColumnStatistic.hasNaNValue() || multiInMatchStatistic.hasNaNValue()) {
                return true;
            }
            if (multiInMatchTuple.get(i).isConstantNull()) {
                return multiInColumnStatistic.getNullsFraction() > 0;
            }
            if (multiInColumns.get(i).getType().getPrimitiveType().isCharFamily()) {
                return true;
            }
            return Math.max(multiInColumnStatistic.getMinValue(), multiInMatchStatistic.getMinValue()) <=
                    Math.min(multiInColumnStatistic.getMaxValue(), multiInMatchStatistic.getMaxValue());
        });
    }

    /**
     * CELONIS_MULTI_IN selectivity is computed according to the following steps:
     * - for each match tuple, verify whether it falls in the range of all input columns or not.
     * - if there is a non-overlapping match the selectivity of the whole match tuple is set to 0.
     * - if all matches are overlapping, compute the selectivity of the match as: nulls fraction of the input column if the
     * match constant is NULL, otherwise 1 / number of distinct values in column.
     * - compute the selectivity of the match tuple by multiplying the selectivities of the individual matches. This assumes the
     * independence of the columns.
     * - compute the selectivity of CELONIS_MULTI_IN by adding the selectivities of the individual matches tuples. This assumes
     * there are no duplicate match tuples.
     */
    private static Statistics estimateCelonisMultiIn(CallOperator call, Statistics statistics) {
        ScalarOperator firstChild = getChildForCastOperator(call.getChild(0));
        ScalarOperator secondChild = getChildForCastOperator(call.getChild(1));

        List<ScalarOperator> multiInColumns = extractInputs(firstChild);
        List<ScalarOperator> multiInMatches = extractInputs(secondChild);
        if (multiInColumns == null || multiInMatches == null || multiInMatches.isEmpty() ||
                multiInColumns.size() != multiInMatches.size()) {
            return null;
        }

        List<List<ScalarOperator>> multiInMatchTuples = transformToMatchTuples(multiInMatches);
        if (multiInMatchTuples == null) {
            return null;
        }

        int multiInColumnCount = multiInColumns.size();
        List<ColumnStatistic> multiInColumnsStatistics = multiInColumns.stream()
                .map(c -> ExpressionStatisticCalculator.calculate(c, statistics)).collect(
                        Collectors.toList());

        List<Boolean> resultUnknownOrNan = new ArrayList<>(Collections.nCopies(multiInColumnCount, false));
        List<Double> resultColumnsMinVals = new ArrayList<>(Collections.nCopies(multiInColumnCount, Double.POSITIVE_INFINITY));
        List<Double> resultColumnsMaxVals = new ArrayList<>(Collections.nCopies(multiInColumnCount, Double.NEGATIVE_INFINITY));
        List<Double> resultColumnsDistinctValCounts = new ArrayList<>(Collections.nCopies(multiInColumnCount, 0.0));
        // For each input column computes the selectivity of the match tuples where the column is matched to null. This gives us
        // the fraction of remaining null values after the evaluation of CELONIS_MULTI_IN.
        // For CELONIS_MULTI_IN ( ( col1 , col2 ), ( [ 1 , NULL ] , [ 2, 3 ] ) ):
        // - the nulls selectivity of col1 is: col1 nulls fraction * ( 1 / col2 distinct count ).
        // - the nulls selectivity of col2 is: 0.
        // The nulls selectivity is used to estimate the qualifying nulls per input column: row count * nulls selectivity, which
        // is used to estimate the nulls fraction after CELONIS_MULTI_IN: qualifying nulls / qualifying tuples.
        List<Double> resultColumnsNullsSelectivity = new ArrayList<>(Collections.nCopies(multiInColumnCount, 0.0));
        double selectivity = 0;

        for (List<ScalarOperator> multiInMatchTuple : multiInMatchTuples) {
            List<ColumnStatistic> multiInMatchTupleStatistics = multiInMatchTuple.stream()
                    .map(c -> ExpressionStatisticCalculator.calculate(c, statistics)).collect(
                            Collectors.toList());
            double tupleSelectivity = 0;
            if (isOverlappingMatchTuple(multiInMatchTuple, multiInMatchTupleStatistics, multiInColumns,
                    multiInColumnsStatistics)) {
                // Compute the selectivity of the tuple of matches by assuming the independence of the individual matches.
                for (int i = 0; i < multiInMatchTuple.size(); ++i) {
                    ScalarOperator match = multiInMatchTuple.get(i);
                    ColumnStatistic matchStatistic = multiInMatchTupleStatistics.get(i);
                    double matchMinVal = matchStatistic.getMinValue();
                    double matchMaxVal = matchStatistic.getMaxValue();

                    ColumnStatistic multiInColumnStatistic = multiInColumnsStatistics.get(i);
                    double multiInColumnDistinctValCount = Math.max(multiInColumnStatistic.getDistinctValuesCount(), 1);

                    if (multiInColumnStatistic.isUnknown() || matchStatistic.isUnknown() ||
                            multiInColumnStatistic.hasNaNValue() || matchStatistic.hasNaNValue()) {
                        tupleSelectivity = i == 0 ? StatisticsEstimateCoefficient.IN_PREDICATE_DEFAULT_FILTER_COEFFICIENT :
                                tupleSelectivity * StatisticsEstimateCoefficient.IN_PREDICATE_DEFAULT_FILTER_COEFFICIENT;
                        resultUnknownOrNan.set(i, true);
                    } else if (match.isConstantNull()) {
                        double nullsFraction = multiInColumnStatistic.getNullsFraction();
                        tupleSelectivity = i == 0 ? nullsFraction : tupleSelectivity * nullsFraction;
                    } else {
                        double matchSelectivity = 1.0 / multiInColumnDistinctValCount;
                        tupleSelectivity = i == 0 ? matchSelectivity : tupleSelectivity * matchSelectivity;
                        // update the column statistic after the predicate.
                        resultColumnsMinVals.set(i, Math.min(resultColumnsMinVals.get(i), matchMinVal));
                        resultColumnsMaxVals.set(i, Math.max(resultColumnsMaxVals.get(i), matchMaxVal));
                        resultColumnsDistinctValCounts.set(i, resultColumnsDistinctValCounts.get(i) + 1);
                    }
                }

                double finalTupleSelectivity = tupleSelectivity;
                IntStream.range(0, multiInColumnCount)
                        .filter(i -> multiInMatchTuple.get(i).isConstantNull())
                        .forEach(i -> resultColumnsNullsSelectivity
                                .set(i, Math.min(multiInColumnsStatistics.get(i).getNullsFraction(),
                                        resultColumnsNullsSelectivity.get(i) + finalTupleSelectivity)));
            }

            // add the individual match tuple selectivities together assuming no duplicates.
            selectivity += tupleSelectivity;
        }

        double inputRowCount = statistics.getOutputRowCount();
        double rowCount = Math.min(inputRowCount, inputRowCount * selectivity);

        // compute result columns nulls fraction
        List<Double> resultColumnsNullsFractions = resultColumnsNullsSelectivity.stream()
                .map(nullsFraction -> rowCount == 0 ? 0 : (nullsFraction * inputRowCount) / rowCount)
                .collect(Collectors.toList());

        // set the updated column statistics after the predicate.
        List<ColumnStatistic> resultColumnStatistics = IntStream.range(0, multiInColumnCount)
                .mapToObj(i -> ColumnStatistic.buildFrom(multiInColumnsStatistics.get(i))
                        .setMinValue(resultColumnsMinVals.get(i))
                        .setMaxValue(resultColumnsMaxVals.get(i))
                        .setDistinctValuesCount(resultColumnsDistinctValCounts.get(i))
                        .setNullsFraction(resultColumnsNullsFractions.get(i))
                        .build())
                .collect(Collectors.toList());
        Statistics.Builder builder = Statistics
                .buildFrom(statistics)
                .setOutputRowCount(rowCount);
        IntStream.range(0, multiInColumnCount)
                .filter(i -> multiInColumns.get(i).isColumnRef() && !resultUnknownOrNan.get(i))
                .forEach(i -> builder
                        .addColumnStatistic(((ColumnRefOperator) multiInColumns.get(i)), resultColumnStatistics.get(i)));

        return StatisticsEstimateUtils.adjustStatisticsByRowCount(builder.build(), rowCount);
    }
}
