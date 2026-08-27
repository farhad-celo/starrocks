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
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CastOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashCalculationException;
import org.apache.commons.math3.distribution.NormalDistribution;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import static java.lang.Double.NEGATIVE_INFINITY;
import static java.lang.Double.POSITIVE_INFINITY;

public class CelonisExpressionStatisticsCalculator {

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
                histogram = CelonisHashMcvProjector.projectSingleArgHash(callOperator, columnStatistic, rowCount);
                break;
            case FunctionSet.CELONIS_XX_HASH3_128_NULLABLE:
                minValue = LargeIntLiteral.LARGE_INT_MIN.doubleValue();
                maxValue = LargeIntLiteral.LARGE_INT_MAX.doubleValue();
                histogram = CelonisHashMcvProjector.projectSingleArgHash(callOperator, columnStatistic, rowCount);
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
            case FunctionSet.CELONIS_STRING_TO_DOUBLE:
            case FunctionSet.CELONIS_TO_DOUBLE:
                averageRowSize = ScalarType.DOUBLE.getTypeSize();
                break;
            case FunctionSet.CELONIS_STRING_TO_INT:
                averageRowSize = ScalarType.BIGINT.getTypeSize();
                break;
            case FunctionSet.CELONIS_QNORM:
                if (maxValue <= 0 || minValue >= 1) {
                    // All outputs are NULL since max >= min holds. The BE implementation returns NULL for inputs outside the
                    // (0,1) interval.
                    minValue = NEGATIVE_INFINITY;
                    maxValue = POSITIVE_INFINITY;
                    nullsFraction = 1.0;
                    distinctValue = 1;
                } else {
                    final var normalDistribution = new NormalDistribution();
                    minValue = minValue <= 0 ? NEGATIVE_INFINITY : normalDistribution.inverseCumulativeProbability(minValue);
                    maxValue = maxValue >= 1 ? POSITIVE_INFINITY : normalDistribution.inverseCumulativeProbability(maxValue);
                }
                break;
            case FunctionSet.CELONIS_GREATEST:
            case FunctionSet.CELONIS_LEAST:
            case FunctionSet.CELONIS_UPPER:
            case FunctionSet.CELONIS_LOWER:
            case FunctionSet.CELONIS_SANITIZE_INVALID_UTF8:
            case FunctionSet.CELONIS_STRINGHASH:
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

        return ColumnStatistic.unknown().getAverageRowSize(); // default value as fallback
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

    private static ColumnStatistic celonisGreatestLeastCalculate(CallOperator callOperator,
                                                                 List<ColumnStatistic> childrenColumnStatistics,
                                                                 double rowCount) {
        if (childrenColumnStatistics.stream().anyMatch(ColumnStatistic::isUnknown)) {
            return null;
        }

        double minValue = callOperator.getFnName().equalsIgnoreCase(FunctionSet.CELONIS_GREATEST) ?
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getMinValue).max().orElse(NEGATIVE_INFINITY) :
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getMinValue).min().orElse(NEGATIVE_INFINITY);
        double maxValue = callOperator.getFnName().equalsIgnoreCase(FunctionSet.CELONIS_GREATEST) ?
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getMaxValue).max().orElse(POSITIVE_INFINITY) :
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getMaxValue).min().orElse(POSITIVE_INFINITY);
        // nulls are only retained if all columns are null.
        double nullsFraction = childrenColumnStatistics.stream() //
                .mapToDouble(ColumnStatistic::getNullsFraction) //
                .min() //
                .orElse(ColumnStatistic.unknown().getNullsFraction());
        double distinctValues = Math.min(rowCount,
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getDistinctValuesCount).sum());
        double averageRowSize = callOperator.getType().getPrimitiveType().isCharFamily() ?
                childrenColumnStatistics.stream().mapToDouble(ColumnStatistic::getAverageRowSize).average()
                        .orElse(callOperator.getType().getTypeSize()) :
                callOperator.getType().getTypeSize();

        return ColumnStatistic.builder()
                .setMinValue(minValue)
                .setMaxValue(maxValue)
                .setNullsFraction(nullsFraction)
                .setAverageRowSize(averageRowSize)
                .setDistinctValuesCount(distinctValues)
                .build();
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
            case FunctionSet.CELONIS_DEDUP_SORTED_BY:
            case FunctionSet.CELONIS_LTRIM:
            case FunctionSet.CELONIS_RTRIM:
                // Use first child statistics
                break;
            case FunctionSet.CELONIS_STRING_ARRAY_JOIN:
                averageRowSize = left.getAverageRowSize() + Math.max(0, left.getCollectionSize() * right.getAverageRowSize());
                collectionSize = ColumnStatistic.DEFAULT_COLLECTION_SIZE;
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
            case FunctionSet.CELONIS_GREATEST:
            case FunctionSet.CELONIS_LEAST:
                return celonisGreatestLeastCalculate(callOperator, List.of(left, right), rowCount);
            case FunctionSet.CELONIS_XX_HASH3_128:
            case FunctionSet.CELONIS_XX_HASH3_128_V2:
            case FunctionSet.CELONIS_XX_HASH3_128_V3:
            case FunctionSet.CELONIS_XX_HASH3_128_V4:
                return calculateNonNullableMultiArgCelonisHashStats(callOperator, List.of(left, right), rowCount);
            case FunctionSet.CELONIS_XX_HASH3_128_NULLABLE:
                minValue = LargeIntLiteral.LARGE_INT_MIN.doubleValue();
                maxValue = LargeIntLiteral.LARGE_INT_MAX.doubleValue();
                distinctValues = Math.min(rowCount, Math.max(left.getDistinctValuesCount(), right.getDistinctValuesCount()));
                averageRowSize = callOperator.getType().getTypeSize();
                collectionSize = ColumnStatistic.DEFAULT_COLLECTION_SIZE;
                break;
            case FunctionSet.CELONIS_LIKE:
                minValue = 0;
                maxValue = 1;
                distinctValues = 2;
                averageRowSize = callOperator.getType().getTypeSize();
                break;
            case FunctionSet.CELONIS_SORTED_FIRST:
            case FunctionSet.CELONIS_SORTED_LAST:
                return calculateCelonisSortedFirstLast(callOperator, left, rowCount);
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
        final var inputColumnStatistic = childrenColumnStatistics.get(0);
        if (inputColumnStatistic.isUnknown()) {
            return null;
        }

        double minValue = POSITIVE_INFINITY;
        double maxValue = NEGATIVE_INFINITY;
        double distinctValue = Math.min(inputColumnStatistic.getDistinctValuesCount(), rowCount);
        double averageRowSize = inputColumnStatistic.getAverageRowSize();
        double nullsFraction = 0;
        ConstantOperator defaultValue = null;

        double averageRowSizeTotal = 0.0;
        double averageRowSizeCount = 0.0;
        double columnNonNullsFraction = 1 - inputColumnStatistic.getNullsFraction();
        boolean isNullMappedToNonNull = false;
        boolean isNullMappedToNull = false;

        final var deduplicatedValuesMap = deduplicateMappedValues(children, inputColumnStatistic, inputStatistics);
        if (deduplicatedValuesMap == null) {
            return null;
        }

        for (final var mappedPair : deduplicatedValuesMap.entrySet()) {
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
            final var inputExpression = children.get(0);
            final var defaultExpression = children.get(3);

            if (getChildForCastOperator(inputExpression).equals(getChildForCastOperator(defaultExpression))) {
                // Input equals default, we can use the input stats.
                minValue = Math.min(minValue, inputColumnStatistic.getMinValue());
                maxValue = Math.max(maxValue, inputColumnStatistic.getMaxValue());
            } else if (defaultExpression instanceof ConstantOperator constantDefaultExpression) {
                defaultValue = constantDefaultExpression;
                int countDistinctMappedToNonNullValues = countDistinctMappedToNonNullValues(deduplicatedValuesMap,
                        defaultValue);

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
                return null;
            }
        } else {
            minValue = Math.min(minValue, inputColumnStatistic.getMinValue());
            maxValue = Math.max(maxValue, inputColumnStatistic.getMaxValue());
        }

        // Nulls from the input columns are preserved only if nulls are not explicitly mapped to another constant, or if they
        // are explicitly mapped to null.
        boolean preserveNulls = isNullMappedToNull || !isNullMappedToNonNull;
        if (preserveNulls) {
            nullsFraction += inputColumnStatistic.getNullsFraction();
        }

        nullsFraction = Math.min(1.0, nullsFraction);
        final var histogram = projectHistogramThroughRemapValues(inputColumnStatistic, deduplicatedValuesMap, defaultValue,
                nullsFraction);

        return ColumnStatistic.builder()
                .setMinValue(minValue == POSITIVE_INFINITY ? NEGATIVE_INFINITY : minValue) // Avoid unset min
                .setMaxValue(maxValue  == NEGATIVE_INFINITY ? POSITIVE_INFINITY : maxValue) // Avoid unset max
                .setNullsFraction(nullsFraction)
                .setAverageRowSize(averageRowSize)
                .setDistinctValuesCount(distinctValue)
                .setHistogram(histogram)
                .build();
    }

    private static Histogram projectHistogramThroughRemapValues(ColumnStatistic inputColumnStatistic,
                                                                Map<ConstantOperator, ConstantOperator> valuesMap,
                                                                ConstantOperator defaultValue,
                                                                double outputNullsFraction) {
        final var inputHistogram = inputColumnStatistic.getHistogram();
        if (inputHistogram == null || inputHistogram.getMCV() == null) {
            return null;
        }

        final var inputMcvs = inputHistogram.getMCV();
        final var projectedMcvs = new HashMap<String, Long>();

        for (final var mappedPair : valuesMap.entrySet()) {
            final var mappedFrom = mappedPair.getKey();
            final var mappedTo = mappedPair.getValue();

            // NULL is not modeled via MCVs.
            if (mappedTo.isNull()) {
                continue;
            }

            // Map NULL input to MCV.
            if (mappedFrom.isNull()) {
                // Histograms do not carry NULL as an MCV key, so estimate this part from null fraction.
                final var keyOpt = toMcvKey(mappedTo);
                keyOpt.ifPresent(key -> projectedMcvs.merge(key,
                        (long) (inputHistogram.getTotalRows() * inputColumnStatistic.getNullsFraction()), Long::sum));
                continue;
            }

            // Map old MCV to new MCV.
            final var mappedFromKeyOpt = toMcvKey(mappedFrom);
            mappedFromKeyOpt.ifPresent(mappedFromKey -> {
                final var mappedFromRowCount = inputMcvs.get(mappedFromKey);
                if (mappedFromRowCount != null && mappedFromRowCount > 0) {
                    final var mappedToKeyOpt = toMcvKey(mappedTo);
                    mappedToKeyOpt.ifPresent(mappedToKey -> projectedMcvs.merge(mappedToKey, mappedFromRowCount, Long::sum));
                }
            });

        }

        // Map everything else (inferred by non-null rows + existing mapped MCVs) to the default value.
        if (defaultValue != null && !defaultValue.isNull()) {
            final long unmappedMcvRows = inputMcvs.keySet().stream() //
                    .filter(mcvKey -> !projectedMcvs.containsKey(mcvKey)) //
                    .mapToLong(inputMcvs::get) //
                    .sum(); //
            final long nonNullRows = (long) (inputHistogram.getTotalRows() * (1.0 - outputNullsFraction));
            final long knownFromMcvRows = projectedMcvs.values().stream().mapToLong(Long::longValue).sum();
            final long unmappedNonMcvRows = nonNullRows - knownFromMcvRows;

            final var defaultRows = unmappedMcvRows + unmappedNonMcvRows;

            if (defaultRows > 0) {
                final var keyOpt = toMcvKey(defaultValue);
                keyOpt.ifPresent((key) -> projectedMcvs.merge(key, defaultRows, Long::sum));
            }
        }

        if (projectedMcvs.isEmpty()) {
            return null;
        }
        return new Histogram(List.of(), projectedMcvs);
    }

    private static Optional<String> toMcvKey(ConstantOperator operator) {
        return operator.castTo(Type.VARCHAR).map(ConstantOperator::toString);
    }

    private static ColumnStatistic multiaryExpressionCalculate(CallOperator callOperator,
                                                               List<ColumnStatistic> childrenColumnStatistics,
                                                               Statistics inputStatistics, double rowCount) {
        // Can't be null since this method is only called when > 2 args.
        final var firstChildStats = childrenColumnStatistics.get(0);
        final var secondChildStats = childrenColumnStatistics.get(1);

        switch (callOperator.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_CALCULATE_RANGE_END:
            case FunctionSet.CELONIS_CALC_CROP:
            case FunctionSet.CELONIS_CALC_CROP_TO_NULL:
            case FunctionSet.CELONIS_MERGE_SORTED_ARRAYS:
            case FunctionSet.CELONIS_STRING_SPLIT:
            case FunctionSet.CELONIS_TRANSLATE:
                // use first child statistics.
                return firstChildStats;
            case FunctionSet.CELONIS_GREATEST:
            case FunctionSet.CELONIS_LEAST:
                return celonisGreatestLeastCalculate(callOperator, childrenColumnStatistics, rowCount);
            case FunctionSet.CELONIS_PEEK_MERGED_SORTED_ARRAYS:
                final var averageRowSize = estimateAverageRowSizeForItemInArray(callOperator.getChild(0),
                        firstChildStats.getCollectionSize(), firstChildStats.getAverageRowSize());
                return ColumnStatistic.builder() //
                        .setAverageRowSize(averageRowSize) //
                        .build();
            case FunctionSet.CELONIS_REMAP_VALUES:
            case FunctionSet.CELONIS_REMAP_VALUES_CONST:
                return celonisRemapValuesCalculate(callOperator.getChildren(), childrenColumnStatistics, inputStatistics,
                        rowCount);
            case FunctionSet.CELONIS_PATINDEX:
                // Re-use binary implementation since third argument does not change stats.
                return binaryExpressionCalculate(callOperator, firstChildStats, secondChildStats, rowCount);
            case FunctionSet.CELONIS_XX_HASH3_128:
            case FunctionSet.CELONIS_XX_HASH3_128_V2:
            case FunctionSet.CELONIS_XX_HASH3_128_V3:
            case FunctionSet.CELONIS_XX_HASH3_128_V4:
                return calculateNonNullableMultiArgCelonisHashStats(callOperator, childrenColumnStatistics, rowCount);
            case FunctionSet.CELONIS_XX_HASH3_128_NULLABLE: {
                if (childrenColumnStatistics.stream().anyMatch(ColumnStatistic::isUnknown)) {
                    return null;
                }

                double maxNdv = childrenColumnStatistics.stream() //
                        .mapToDouble(ColumnStatistic::getDistinctValuesCount) //
                        .max() //
                        .orElse(ColumnStatistic.unknown().getDistinctValuesCount());
                // Probability of at least one child being NULL
                double combinedNullsFraction = 1.0 - childrenColumnStatistics.stream() //
                        .mapToDouble(childStat -> 1.0 - childStat.getNullsFraction()) //
                        .reduce(1.0, (firstNullFraction, secondNullFraction) -> firstNullFraction * secondNullFraction);
                return ColumnStatistic.builder() //
                        .setMinValue(LargeIntLiteral.LARGE_INT_MIN.doubleValue()) //
                        .setMaxValue(LargeIntLiteral.LARGE_INT_MAX.doubleValue()) //
                        .setNullsFraction(combinedNullsFraction) //
                        .setAverageRowSize(callOperator.getType().getTypeSize()) //
                        .setDistinctValuesCount(Math.min(rowCount, maxNdv)) //
                        .build();
            }
            case FunctionSet.CELONIS_SORTED_FIRST:
            case FunctionSet.CELONIS_SORTED_LAST:
                return calculateCelonisSortedFirstLast(callOperator, firstChildStats, rowCount);
            default:
                return null;
        }
    }


    /**
     * Computes the statistics of a multi-argument, non-nullable Celonis hash call. The MCVs of the arguments are
     * projected through the call for the hash functions that have a Java implementation for multiple inputs, the others
     * just keep their base statistics.
     */
    private static ColumnStatistic calculateNonNullableMultiArgCelonisHashStats(CallOperator callOperator,
                                                                        List<ColumnStatistic> childrenColumnStatistics,
                                                                        double rowCount) {
        final var hashStats = calculateNonNullableMultiArgCelonisHashBaseStats(callOperator, childrenColumnStatistics, rowCount);
        if (hashStats == null) {
            return null;
        }

        final Histogram projectedHistogram;
        try {
            projectedHistogram = CelonisHashMcvProjector.projectMultiArgHash(callOperator, childrenColumnStatistics,
                    rowCount);
        } catch (CelonisHashCalculationException exception) {
            return hashStats;
        }

        if (projectedHistogram == null) {
            return hashStats;
        }

        return ColumnStatistic.buildFrom(hashStats).setHistogram(projectedHistogram).build();
    }

    private static ColumnStatistic calculateNonNullableMultiArgCelonisHashBaseStats(CallOperator callOperator,
                                                                           List<ColumnStatistic> childrenColumnStatistics,
                                                                           double rowCount) {
        if (childrenColumnStatistics.stream().anyMatch(ColumnStatistic::isUnknown)) {
            return null;
        }

        double maxNdv = childrenColumnStatistics.stream() //
                .mapToDouble(ColumnStatistic::getDistinctValuesCount) //
                .max() //
                .orElse(ColumnStatistic.unknown().getDistinctValuesCount());
        return ColumnStatistic.builder() //
                .setMinValue(LargeIntLiteral.LARGE_INT_MIN.doubleValue()) //
                .setMaxValue(LargeIntLiteral.LARGE_INT_MAX.doubleValue()) //
                .setNullsFraction(0.0) //
                .setAverageRowSize(callOperator.getType().getTypeSize()) //
                .setDistinctValuesCount(Math.min(rowCount, maxNdv)) //
                .build();
    }

    private static ColumnStatistic calculateCelonisSortedFirstLast(CallOperator callOperator,
                                                                   ColumnStatistic childColumnStatistics,
                                                                   double rowCount) {
        if (childColumnStatistics.isUnknown()) {
            return null;
        }

        boolean isStaticSizeType = isStaticSizeType(callOperator.getType());
        double averageRowSize = isStaticSizeType ? callOperator.getType().getTypeSize() 
                : childColumnStatistics.getAverageRowSize();

        return ColumnStatistic.buildFrom(childColumnStatistics) //
                .setDistinctValuesCount(Math.min(rowCount, childColumnStatistics.getDistinctValuesCount())) //
                .setAverageRowSize(averageRowSize) //
                .build();

    }
}
