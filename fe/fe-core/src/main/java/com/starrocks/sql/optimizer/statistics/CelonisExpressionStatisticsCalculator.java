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

import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.starrocks.analysis.LargeIntLiteral;
import com.starrocks.catalog.ArrayType;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.ScalarType;
import com.starrocks.catalog.Type;
import com.starrocks.metric.LongCounterMetric;
import com.starrocks.metric.Metric;
import com.starrocks.metric.MetricRepo;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CastOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashFunction;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.annotation.Nullable;

import static java.lang.Double.NEGATIVE_INFINITY;
import static java.lang.Double.POSITIVE_INFINITY;

public class CelonisExpressionStatisticsCalculator {
    private static final ConcurrentHashMap<String, LongCounterMetric> COUNTERS = new ConcurrentHashMap<>();

    public static ColumnStatistic callCalculate(CallOperator call, List<ColumnStatistic> childrenColumnStatistics,
                                                Statistics inputStatistics, double rowCount) {
        if (call.getChildren().size() == 1) {
            return unaryExpressionCalculate(call, childrenColumnStatistics.get(0), rowCount);
        } else if (call.getChildren().size() == 2) {
            return binaryExpressionCalculate(call, childrenColumnStatistics.get(0), childrenColumnStatistics.get(1), rowCount);
        } else if (call.getChildren().size() > 2) {
            return multiaryExpressionCalculate(call, childrenColumnStatistics, inputStatistics, rowCount);
        }

        return null;
    }

    private static boolean isStaticSizeType(Type type) {
        return type.isIntegerType() || type.isFloatingPointType() || type.isDateType() ||
                type.isBitmapType() || type.isFixedPointType();
    }

    private static ColumnStatistic unaryExpressionCalculate(CallOperator callOperator, ColumnStatistic columnStatistic,
                                                            double rowCount) {
        if (columnStatistic.isUnknown()) {
            return null;
        }

        boolean isStaticSizeType = isStaticSizeType(callOperator.getType());
        double minValue = columnStatistic.getMinValue();
        double maxValue = columnStatistic.getMaxValue();
        double distinctValue = Math.min(rowCount, columnStatistic.getDistinctValuesCount());
        double nullsFraction = columnStatistic.getNullsFraction();
        double averageRowSize = isStaticSizeType ? callOperator.getType().getTypeSize() : columnStatistic.getAverageRowSize();
        // Per default, use the DEFAULT_COLLECTION_SIZE and only set this for expressions returning arrays.
        double collectionSize = ColumnStatistic.DEFAULT_COLLECTION_SIZE;

        var histogram = columnStatistic.getHistogram();

        switch (callOperator.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_XX_HASH3_128:
            case FunctionSet.CELONIS_XX_HASH3_128_V2:
            case FunctionSet.CELONIS_XX_HASH3_128_V3:
            case FunctionSet.CELONIS_XX_HASH3_128_V4:
                minValue = LargeIntLiteral.LARGE_INT_MIN.doubleValue();
                maxValue = LargeIntLiteral.LARGE_INT_MAX.doubleValue();
                nullsFraction = 0.0;
                histogram = projectHistogramThroughHash(histogram, callOperator, columnStatistic, rowCount);
                break;
            case FunctionSet.CELONIS_XX_HASH3_128_NULLABLE:
                minValue = LargeIntLiteral.LARGE_INT_MIN.doubleValue();
                maxValue = LargeIntLiteral.LARGE_INT_MAX.doubleValue();
                break;
            case FunctionSet.CELONIS_ARRAY_COUNT:
            case FunctionSet.CELONIS_ARRAY_COUNT_DISTINCT:
                minValue = 0;
                maxValue = POSITIVE_INFINITY;
                averageRowSize = ScalarType.BIGINT.getTypeSize();
                break;
            case FunctionSet.CELONIS_ARRAY_FIRST:
            case FunctionSet.CELONIS_ARRAY_LAST:
                averageRowSize = estimateAverageRowSizeForItemInArray(callOperator.getChild(0),
                        collectionSize, averageRowSize);
                break;
            case FunctionSet.CELONIS_ARRAY_AVG:
            case FunctionSet.CELONIS_ARRAY_TRIMMED_MEAN:
                averageRowSize = ScalarType.DOUBLE.getTypeSize();
                break;
            case FunctionSet.CELONIS_ARRAY_BOOL_OR:
                minValue = 0;
                maxValue = 1;
                distinctValue = 3;
                break;
            case FunctionSet.CELONIS_SQUARE:
                double celonisSquareMinValue;
                double celonisSquareMaxValue = Math.max(minValue * minValue, maxValue * maxValue);
                if ((minValue < 0 && maxValue < 0) || (minValue >= 0 && maxValue >= 0)) {
                    celonisSquareMinValue = Math.min(minValue * minValue, maxValue * maxValue);
                } else {
                    celonisSquareMinValue = 0;
                }
                minValue = celonisSquareMinValue;
                maxValue = celonisSquareMaxValue;
                break;
            case FunctionSet.CELONIS_GREATEST:
            case FunctionSet.CELONIS_LEAST:
            case FunctionSet.CELONIS_UPPER:
            case FunctionSet.CELONIS_LOWER:
            case FunctionSet.CELONIS_TO_DOUBLE:
            case FunctionSet.CELONIS_STRING_TO_DOUBLE:
                // Just use the input's statistics as output's statistics
                break;
            default:
                return null;
        }

        return ColumnStatistic.builder()
                .setMinValue(minValue)
                .setMaxValue(maxValue)
                .setNullsFraction(nullsFraction)
                .setAverageRowSize(averageRowSize)
                .setDistinctValuesCount(distinctValue)
                .setHistogram(histogram)
                .setCollectionSize(collectionSize)
                .build();
    }

    /* Logs metrics on how often we propagate the histogram. */
    private static void logHistogramHashProjection() {
        COUNTERS.computeIfAbsent("celonis_hash_mcv_propagation", k -> {
            LongCounterMetric metric = new LongCounterMetric("celonis_hash_mcv_propagation", Metric.MetricUnit.NOUNIT,
                    "Amount of propagated Celonis hash MCVs");
            MetricRepo.addMetric(metric);
            return metric;
        }).increase(1L);
    }

    private static Histogram projectHistogramThroughHash(@Nullable Histogram histogram,
                                                         CallOperator callOperator, ColumnStatistic columnStatistic,
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
        projectedMcvs.put(hashFunction.computeNull().toString(),
                (long) (rowCount * columnStatistic.getNullsFraction()));

        if (histogram != null) {
            // Project other (non-null) MCVs
            projectedMcvs.putAll(histogram.getMCV() //
                    .entrySet() //
                    .stream() //
                    .collect(Collectors.toMap(key -> hashFunction.compute(key.getKey()).toString(), Map.Entry::getValue)));
        }

        logHistogramHashProjection();
        return new Histogram(List.of(), projectedMcvs);
    }

    private static Optional<Type> extractArrayItemType(ScalarOperator operator) {
        if (operator.getType().isArrayType()) {
            final var castedType = (ArrayType) operator.getType();
            return Optional.of(castedType.getItemType());
        }

        return Optional.empty();
    }

    private static double estimateAverageRowSizeForItemInArray(ScalarOperator arrayOperator, double arrayCollectionSize,
                                                               double arrayAverageRowSize) {
        final var arrayItemTypeOpt = extractArrayItemType(arrayOperator);
        if (arrayItemTypeOpt.isPresent()) {
            final var arrayItemType = arrayItemTypeOpt.get();
            if (isStaticSizeType(arrayItemType)) {
                // If it is a static type we know exactly how large the row will be.
                return arrayItemType.getTypeSize();
            } else if (arrayCollectionSize > 0) {
                // If it is a dynamic type, we can approximate using the array stats.
                return arrayAverageRowSize / arrayCollectionSize;
            }
        }

        return 1; // default value as fallback
    }

    private static ScalarOperator getChildForCastOperator(ScalarOperator operator) {
        while (operator instanceof CastOperator) {
            operator = operator.getChild(0);
        }
        return operator;
    }

    private static boolean hasOverlap(ColumnStatistic left, ColumnStatistic right, boolean isCharType) {
        if (isCharType) {
            // Assume char types always overlap.
            return true;
        }

        return Math.max(left.getMinValue(), right.getMinValue()) <= Math.min(left.getMaxValue(), right.getMaxValue());
    }

    // Deduplicates the mappings to preserve only the last occurrence of each mapped value, because if a value is mapped
    // multiple times, only the last mapping is considered.
    private static Map<ConstantOperator, ConstantOperator> deduplicateMappedValues(List<ScalarOperator> children,
                                                                                   ColumnStatistic columnStatistic,
                                                                                   Statistics statistics) {
        boolean isCharType = getChildForCastOperator(children.get(0)).getType().getPrimitiveType().isCharFamily();
        ScalarOperator mappedFrom = getChildForCastOperator(children.get(1));
        ScalarOperator mappedTo = getChildForCastOperator(children.get(2));
        if (!(mappedFrom instanceof ArrayOperator) || !(mappedTo instanceof ArrayOperator) ||
                mappedFrom.getChildren().size() != mappedTo.getChildren().size()) {
            return null;
        }

        List<ScalarOperator> mappedFromArray = mappedFrom.getChildren();
        List<ScalarOperator> mappedToArray = mappedTo.getChildren();

        Map<ConstantOperator, ConstantOperator> deduplicatedValuesMap = Maps.newHashMap();
        for (int i = mappedFromArray.size() - 1; i >= 0; --i) {
            ScalarOperator mappedFromValue = mappedFromArray.get(i);
            ScalarOperator mappedToValue = mappedToArray.get(i);
            if (!(mappedFromValue instanceof ConstantOperator) || !(mappedToValue instanceof ConstantOperator)) {
                return null;
            }

            ConstantOperator mappedFromConstant = (ConstantOperator) mappedFromValue;
            ConstantOperator mappedToConstant = (ConstantOperator) mappedToValue;

            if (!hasOverlap(columnStatistic, ExpressionStatisticCalculator.calculate(mappedFromConstant, statistics),
                    isCharType)) {
                continue;
            }

            deduplicatedValuesMap.putIfAbsent(mappedFromConstant, mappedToConstant);
        }

        return deduplicatedValuesMap;
    }

    // Counts the number of distinct values after the mapping is applied, because multiple FROM values could be mapped
    // to the same TO value.
    private static int countDistinctMappedToNonNullValues(Map<ConstantOperator, ConstantOperator> valuesMap,
                                                          ConstantOperator defaultValue) {
        HashSet<ConstantOperator> distinctValues = Sets.newHashSet();
        for (ConstantOperator value : valuesMap.values()) {
            if (!value.isNull()) {
                distinctValues.add(value);
            }
        }

        if (!defaultValue.isNull()) {
            distinctValues.add(defaultValue);
        }
        return distinctValues.size();
    }

    private static ColumnStatistic binaryExpressionCalculate(CallOperator callOperator, ColumnStatistic left,
                                                             ColumnStatistic right, double rowCount) {
        if (left.isUnknown() || right.isUnknown()) {
            return null;
        }

        double minValue = left.getMinValue();
        double maxValue = left.getMaxValue();
        double distinctValues = left.getDistinctValuesCount();
        double nullsFraction = 1 - ((1 - left.getNullsFraction()) * (1 - right.getNullsFraction()));

        double averageRowSize = left.getAverageRowSize();
        double collectionSize = left.getCollectionSize();

        switch (callOperator.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_ARRAY_LAG:
            case FunctionSet.CELONIS_ARRAY_LEAD:
                // Use first child statistics
                break;
            case FunctionSet.CELONIS_DECODE_STRING:
                minValue = NEGATIVE_INFINITY;
                maxValue = POSITIVE_INFINITY;
                distinctValues = left.getDistinctValuesCount();
                averageRowSize = estimateAverageRowSizeForItemInArray(callOperator.getChild(1), right.getCollectionSize(),
                        right.getAverageRowSize());
                break;
            case FunctionSet.CELONIS_ENCODE_STRING:
                minValue = -1;
                maxValue = left.getDistinctValuesCount() - 1;
                averageRowSize = ScalarType.INT.getTypeSize();
                break;
            case FunctionSet.CELONIS_PATINDEX:
                minValue = 0;
                maxValue =  LargeIntLiteral.LARGE_INT_MAX.doubleValue();
                averageRowSize = ScalarType.BIGINT.getTypeSize();
                collectionSize = ColumnStatistic.DEFAULT_COLLECTION_SIZE;
                break;
            default:
                return null;
        }

        return ColumnStatistic.builder()
                .setMinValue(minValue)
                .setMaxValue(maxValue)
                .setNullsFraction(nullsFraction)
                .setAverageRowSize(averageRowSize)
                .setDistinctValuesCount(distinctValues)
                .setCollectionSize(collectionSize)
                .build();
    }

    /**
     * CELONIS_REMAP_VALUES statistics are computed as following:
     * First the mappings are deduplicated by taking the last occurrence of each unique mappedFrom value.
     * Then the statistics are compute differently depending on whether there is a default mapping or not.
     * Without a default mapping:
     * - max: max ( column max , mappedTo1 , ... )
     * - min: min ( column min , mappedTo1 , ... )
     * - distinct values: column distinct values
     * - nulls fraction: sum ( number of values mapped to NULL / column distinct values ) + column nulls
     * fraction (if nulls are preserved. This is the case when NULL is not mapped or mapped to NULL).
     * - average row size: column average row size
     * With a default mapping:
     * - max: max ( mappedTo1 , ... , mappedToDefault )
     * - min: min ( mappedTo1 , ... , mappedToDefault )
     * - distinct values: distinct values ( mappedTo1 , ... , mappedToDefault )
     * - nulls fraction: sum ( number of values mapped to NULL / column distinct values ) + column nulls
     * fraction (if nulls are preserved. This is the case when NULL mapped to NULL or the default mapping
     * is NULL) + fraction of non-mapped values ( if the default mapping is null).
     * - average row size: column average row size
     */
    private static ColumnStatistic celonisRemapValuesCalculate(List<ScalarOperator> children,
                                                               List<ColumnStatistic> childrenColumnStatistics,
                                                               Statistics inputStatistics, double rowCount) {
        ColumnStatistic columnStatistic = childrenColumnStatistics.get(0);
        if (columnStatistic.isUnknown()) {
            return null;
        }

        double minValue = POSITIVE_INFINITY;
        double maxValue = NEGATIVE_INFINITY;
        double distinctValue = Math.min(columnStatistic.getDistinctValuesCount(), rowCount);
        double averageRowSize = columnStatistic.getAverageRowSize();
        double nullsFraction = 0;

        double averageRowSizeTotal = 0.0;
        double averageRowSizeCount = 0.0;
        double columnNonNullsFraction = 1 - columnStatistic.getNullsFraction();
        boolean isNullMappedToNonNull = false;
        boolean isNullMappedToNull = false;

        Map<ConstantOperator, ConstantOperator> deduplicatedValuesMap = deduplicateMappedValues(children, columnStatistic,
                inputStatistics);
        if (deduplicatedValuesMap == null) {
            return null;
        }

        for (Map.Entry<ConstantOperator, ConstantOperator> mappedPair : deduplicatedValuesMap.entrySet()) {
            ConstantOperator mappedFrom = mappedPair.getKey();
            ConstantOperator mappedTo = mappedPair.getValue();

            if (mappedTo.isNull()) {
                if (!mappedFrom.isNull()) {
                    // This formula assumes that the mappedTo value always exists in the input column, which could lead to large
                    // over estimations of the nullsFraction if the number of distinct values in the column is comparable to the
                    // number of mappings.
                    nullsFraction += distinctValue != 0 ? columnNonNullsFraction * (1.0 / distinctValue) : 0;
                } else {
                    isNullMappedToNull = true;
                }
                continue;
            }

            if (mappedFrom.isNull()) {
                isNullMappedToNonNull = true;
            }

            ColumnStatistic mappedToStatistic = ExpressionStatisticCalculator.calculate(mappedTo, inputStatistics);
            minValue = Math.min(minValue, mappedToStatistic.getMinValue());
            maxValue = Math.max(maxValue, mappedToStatistic.getMaxValue());
            averageRowSizeTotal += mappedToStatistic.getAverageRowSize();
            ++averageRowSizeCount;
        }

        if (children.size() == 4) {
            if (!(children.get(3) instanceof ConstantOperator)) {
                return null;
            }

            ConstantOperator defaultValue = (ConstantOperator) children.get(3);
            int countDistinctMappedToNonNullValues = countDistinctMappedToNonNullValues(deduplicatedValuesMap, defaultValue);

            if (defaultValue.isNull()) {
                double nonNullMappedValues = deduplicatedValuesMap.size();
                if (isNullMappedToNonNull || isNullMappedToNull) {
                    nonNullMappedValues -= 1;
                }
                nullsFraction += distinctValue != 0 ?
                        columnNonNullsFraction * Math.max(0, distinctValue - nonNullMappedValues) / distinctValue : 0;
                distinctValue = Math.min(distinctValue, countDistinctMappedToNonNullValues);
            } else {
                ColumnStatistic defaultValueStatistic = childrenColumnStatistics.get(3);
                minValue = Math.min(minValue, defaultValueStatistic.getMinValue());
                maxValue = Math.max(maxValue, defaultValueStatistic.getMaxValue());
                distinctValue = Math.min(distinctValue, countDistinctMappedToNonNullValues);
                averageRowSizeTotal += defaultValueStatistic.getAverageRowSize();
                ++averageRowSizeCount;
                isNullMappedToNonNull = true;
            }

            averageRowSize = averageRowSizeCount == 0 ? 0 : averageRowSizeTotal / averageRowSizeCount;
        } else {
            minValue = Math.min(minValue, columnStatistic.getMinValue());
            maxValue = Math.max(maxValue, columnStatistic.getMaxValue());
        }

        // Nulls from the input columns are preserved only if nulls are not explicitly mapped to another constant, or if they
        // are explicitly mapped to null.
        boolean preserveNulls = isNullMappedToNull || !isNullMappedToNonNull;
        if (preserveNulls) {
            nullsFraction += columnStatistic.getNullsFraction();
        }

        nullsFraction = Math.min(1.0, nullsFraction);

        return ColumnStatistic.builder()
                .setMinValue(minValue)
                .setMaxValue(maxValue)
                .setNullsFraction(nullsFraction)
                .setAverageRowSize(averageRowSize)
                .setDistinctValuesCount(distinctValue)
                .build();
    }

    private static ColumnStatistic multiaryExpressionCalculate(CallOperator callOperator,
                                                               List<ColumnStatistic> childrenColumnStatistics,
                                                               Statistics inputStatistics, double rowCount) {
        // Can't be null since this method is only called when > 2 args.
        final var firstArg = childrenColumnStatistics.get(0);
        final var secondArg = childrenColumnStatistics.get(1);

        switch (callOperator.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_REMAP_VALUES:
            case FunctionSet.CELONIS_REMAP_VALUES_CONST:
                return celonisRemapValuesCalculate(callOperator.getChildren(), childrenColumnStatistics, inputStatistics,
                        rowCount);
            case FunctionSet.CELONIS_PATINDEX:
                // Re-use binary implementation since third argument does not change stats.
                return binaryExpressionCalculate(callOperator, firstArg, secondArg, rowCount);
            default:
                return null;
        }
    }
}
