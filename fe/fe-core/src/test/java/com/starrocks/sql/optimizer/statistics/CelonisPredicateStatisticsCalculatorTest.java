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

import java.util.List;

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
}
