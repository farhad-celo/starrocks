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

import com.starrocks.analysis.LargeIntLiteral;
import com.starrocks.catalog.FunctionSet;
import com.starrocks.catalog.Type;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rewrite.celonis.CelonisHashCalculationException;
import com.starrocks.sql.optimizer.rewrite.celonis.XXHASH3128V3;
import com.starrocks.utframe.UtFrameUtils;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.annotation.Nullable;

import static java.lang.Double.NEGATIVE_INFINITY;
import static java.lang.Double.POSITIVE_INFINITY;
import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

/**
 * Tests the projection of argument MCVs through a Celonis hash call. A single argument keeps the row count of each of its
 * MCVs, as the hash maps every value to exactly one output value.
 * <p>
 * Several arguments have to be combined instead. Each argument value is turned into
 * the fraction of the rows it covers, a combination of argument values covers the product of those fractions, and that
 * product times the row count of the hash is how many rows the combination is reported for. Each fraction is taken of a
 * population of at least that row count, so that a histogram which describes fewer rows than the node has can dilute the
 * fractions but never stretch them. The product is truncated to whole rows, so that no combination is ever reported for a
 * row it does not have. Whatever no combination covers is held by a single bucket, so that the histogram accounts for all
 * of the hash's rows.
 */
public class CelonisHashMcvProjectorTest {

    private static final double ROW_COUNT = 10000;

    private ConnectContext context;

    @BeforeEach
    public void setUp() {
        context = UtFrameUtils.createDefaultCtx();
        context.getSessionVariable().setEnableCelonisHashMcvs(true);
        context.getSessionVariable().setEnableCelonisHashMcvsMultiArg(true);
        context.getSessionVariable().setCelonisHashMcvsMultiArgLimitMcvs(20);
    }

    @AfterEach
    public void tearDown() {
        ConnectContext.remove();
    }

    @Test
    public void projectsEveryMcvOfASingleArgumentAndItsNulls() {
        // GIVEN one argument with two MCVs, 2% of whose rows are NULL.
        final var argument = varcharStatistic(0.02, 9800, Map.of("foo", 1000L, "bar", 500L));

        // WHEN
        final var histogram = projectSingleArg(argument, ROW_COUNT);

        // THEN each MCV keeps the row count it had, as the hash maps every value of the argument to exactly one value of
        // the output, and NULL becomes an MCV of its own taken from the null fraction.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo"), 1000L,
                hash("bar"), 500L,
                hash((String) null), 200L // 2% * 10000
        ));
    }

    @Test
    public void returnsNullForASingleArgumentThatIsNotVarchar() {
        // GIVEN
        final var argument = new ColumnRefOperator(0, Type.INT, "num", true);
        final var statistic = varcharStatistic(0, 10000, Map.of("42", 1000L));

        // WHEN / THEN
        assertNull(projectSingleArg(FunctionSet.CELONIS_XX_HASH3_128_V3, argument, statistic, ROW_COUNT));
    }

    @Test
    public void projectsASingleArgumentRegardlessOfTheMultiArgumentSessionVariable() {
        // GIVEN
        final var argument = varcharStatistic(0, 10000, Map.of("foo", 1000L));

        // WHEN / THEN only the general session variable gates a single argument; the multi-argument one does not.
        context.getSessionVariable().setEnableCelonisHashMcvsMultiArg(false);
        assertNotNull(projectSingleArg(argument, ROW_COUNT));

        context.getSessionVariable().setEnableCelonisHashMcvs(false);
        assertNull(projectSingleArg(argument, ROW_COUNT));
    }

    @Test
    public void projectsEveryCombinationOfTheArgumentValues() {
        // GIVEN two arguments collected over the same 10000 rows the hash is estimated over, of which 200 have a NULL
        // first argument and 100 a NULL second one. So "foo" covers a tenth of the rows, "x" covers 8% of them, and so on.
        final var firstArgument = varcharStatistic(0.02, 9800, Map.of("foo", 1000L, "bar", 500L));
        final var secondArgument = varcharStatistic(0.01, 9900, Map.of("x", 800L, "y", 300L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN every value of the first argument is paired with every value of the second one.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 80L, // 10% * 8% * 10000
                hash("foo", "y"), 30L, // 10% * 3% * 10000
                hash("foo", null), 10L, // 10% * 1% * 10000
                hash("bar", "x"), 40L, // 5% * 8% * 10000
                hash("bar", "y"), 15L, // 5% * 3% * 10000
                hash("bar", null), 5L, // 5% * 1% * 10000
                hash(null, "x"), 16L, // 2% * 8% * 10000
                hash(null, "y"), 5L, // 2% * 3% * 10000 = 5.999999999999999, truncated
                hash(null, null), 2L // 2% * 1% * 10000
        ));
    }

    @Test
    public void projectsOntoTheRowsOfThisNodeRatherThanTheCollectedOnes() {
        // GIVEN the same two arguments as above, still describing the 10000 rows the histograms were collected over, but a
        // hash that only sees a fifth of them because of a filter below it.
        final var firstArgument = varcharStatistic(0.02, 9800, Map.of("foo", 1000L, "bar", 500L));
        final var secondArgument = varcharStatistic(0.01, 9900, Map.of("x", 800L, "y", 300L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 2000);

        // THEN the combinations cover the same fractions of the rows as above, which is a fifth of the row counts. The
        // two NULLs together are expected in less than a whole row of this node and are dropped.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 16L, // 10% * 8% * 2000
                hash("foo", "y"), 6L, // 10% * 3% * 2000
                hash("foo", null), 2L, // 10% * 1% * 2000
                hash("bar", "x"), 8L, // 5% * 8% * 2000
                hash("bar", "y"), 3L, // 5% * 3% * 2000
                hash("bar", null), 1L, // 5% * 1% * 2000
                hash(null, "x"), 3L, // 2% * 8% * 2000 = 3.2
                hash(null, "y"), 1L // 2% * 3% * 2000 = 1.2
        ));
    }

    @Test
    public void neverProjectsMoreRowsThanTheNodeHas() {
        // GIVEN a hash seeing only 100 of the 10000 rows the histograms describe. Propagating the collected counts as they
        // are would claim that a single hash value covers 40 times as many rows as this node has at all.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 5000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 4000L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 100);

        // THEN the combination is reported for a fifth of this node instead, which is the share of the rows it covers.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 20L // 50% * 40% * 100
        ));
    }

    @Test
    public void projectsAtMostAllOfTheNodesRows() {
        // GIVEN two arguments whose MCVs cover every row they were collected over, so that their combinations have to
        // cover every row of this node as well and can not possibly cover more.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("a", 5000L, "b", 5000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("p", 6000L, "q", 4000L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 1000);

        // THEN the four combinations divide the rows of this node between them.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("a", "p"), 300L, // 50% * 60% * 1000
                hash("a", "q"), 200L, // 50% * 40% * 1000
                hash("b", "p"), 300L, // 50% * 60% * 1000
                hash("b", "q"), 200L // 50% * 40% * 1000
        ));
        assertThat(histogram.getMCV().values().stream().mapToLong(Long::longValue).sum()).isEqualTo(1000);
        // No bucket is left to fill, and an empty one would have every value it spans estimated as a row of its own.
        assertThat(histogram.getBuckets()).isEmpty();
        assertThat(histogram.getTotalRows()).isEqualTo(1000);
    }

    @Test
    public void projectsCombinationsOfAConstantArgumentWithoutBuckets() {
        // GIVEN a column and a literal, a constant's statistics carry its calue as an MCV counted over all of this
        // node's rows and no buckets, which is what those rows are, so its MCVs are not the case the bucket check
        // guards against.
        final var arguments = List.<ScalarOperator>of(//
                new ColumnRefOperator(0, Type.VARCHAR, "str", true), //
                ConstantOperator.createVarchar("DE"));
        final var statistics = List.of(//
                varcharStatistic(0, 10000, Map.of("foo", 1000L)), // 
                varcharStatistic(0, new Histogram(List.of(), Map.of("DE", 10000L))));
        
        // WHEN
        final var histogram = project(FunctionSet.CELONIS_XX_HASH3_128_V3, arguments, statistics, ROW_COUNT);

        // THEN the literal covers every row, so the combinations follow the column's fractions alone.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(hash("foo", "DE"), 1000L // 10% * 100% * 10000
        ));
    }

    @Test
    public void reportsTooFewRowsRatherThanTooManyWhereTheHistogramsAreOlderThanTheTable() {
        // GIVEN a hash seeing 50000 rows although the histograms were only collected over 10000 of them, as happens once
        // rows have been loaded since the histograms were collected: the row count keeps up with the load, the histograms
        // do not. Those 40000 rows may well hold values we have never seen, so we do not claim they hold these ones.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 50000);

        // THEN both values are taken as fractions of all 50000 rows rather than of the 10000 we measured, so the
        // combination is reported for fewer rows than the 80 it covered when it was measured, let alone the 400 that
        // assuming the new rows hit the same values would claim. Erring in this direction only costs us the skew we could
        // have exploited, whereas erring in the other direction makes the optimizer trust a number that is not there.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 16L // 2% * 1.6% * 50000
        ));
    }

    @Test
    public void dilutesAnArgumentWhoseHistogramDescribesFewerRowsThanTheNodeHas() {
        // GIVEN two arguments whose histograms were collected at different times, the second one over only 4000 rows.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 4000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN "x" is taken as 8% of all 10000 rows rather than as the 20% of the 4000 rows it was measured over. Its
        // fraction is diluted by everything its histogram does not describe, instead of being applied to rows it never saw.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 80L // 10% * 8% * 10000
        ));
    }

    @Test
    public void doesNotInflateTheFractionsWhereAHistogramUnderstatesTheRowsItDescribes() {
        // GIVEN two arguments whose MCVs cover 5000 rows each and whose histograms report nothing beyond them, although
        // the column really has the 10000 rows this node sees. A histogram can understate the rows its MCVs were counted
        // over: the buckets of a non-char column stop at Config.histogram_max_sample_row_count, so on a large table the
        // MCV counts describe the whole column while the buckets account for only part of it.
        final var firstArgument = varcharStatistic(0, 5000, Map.of("foo", 3000L, "bar", 2000L));
        final var secondArgument = varcharStatistic(0, 5000, Map.of("x", 4000L, "y", 1000L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN every value is taken as the fraction of the 10000 rows it really covers, not of the 5000 the histograms
        // admit to. Dividing by 5000 would call "foo" 60% of the rows instead of 30% and "x" 80% instead of 40%, and
        // because the fractions of the arguments are multiplied, the two errors would multiply into twice the rows for
        // every combination: 2400 rows for ("foo", "x") instead of 1200, and 4 times too many for three such arguments.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 1200L, // 30% * 40% * 10000
                hash("foo", "y"), 300L, // 30% * 10% * 10000
                hash("bar", "x"), 800L, // 20% * 40% * 10000
                hash("bar", "y"), 200L // 20% * 10% * 10000
        ));
    }

    @Test
    public void comparesTheNodesRowsAgainstAllOfTheColumnsRowsAndNotJustItsNonNullOnes() {
        // GIVEN a first argument of which half the rows are NULL, so that its histogram describes 5000 non-NULL rows out of
        // a column of 10000, and a hash seeing 50000 rows so that the node's row count is the larger of the two.
        final var firstArgument = varcharStatistic(0.5, 5000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 50000);

        // THEN the 50000 rows are what both arguments' values are taken as fractions of. Comparing the node's rows against
        // the 5000 non-NULL rows and only then taking the null fraction out would arrive at 100000 rows instead, halving
        // every fraction of this argument, because the null fraction is a fraction of all of the column's rows rather than
        // of the ones its histogram counted. That would report 8 rows for ("foo", "x") instead of 16.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 16L, // 2% * 1.6% * 50000
                hash(null, "x"), 400L // 50% * 1.6% * 50000
        ));
    }

    @Test
    public void holdsTheRowsTheProjectedMcvsDoNotCoverInASingleBucket() {
        // GIVEN two arguments whose combinations cover 105 of the 10000 rows of the hash.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L, "y", 250L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN the other 9895 rows are held by a single bucket, so that the histogram accounts for all 10000 of them
        // instead of claiming that the two combinations are all there is to the hash.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 80L, // 10% * 8% * 10000
                hash("foo", "y"), 25L // 10% * 2.5% * 10000
        ));
        assertThat(histogram.getBuckets()).singleElement().satisfies(bucket -> {
            assertThat(bucket.getCount()).isEqualTo(9895);
            assertThat(bucket.getUpperRepeats()).isEqualTo(0);
            // The bucket spans the range of the hash, which is the whole LARGEINT range.
            assertThat(bucket.getLower()).isEqualTo(LargeIntLiteral.LARGE_INT_MIN.doubleValue());
            assertThat(bucket.getUpper()).isEqualTo(LargeIntLiteral.LARGE_INT_MAX.doubleValue());
        });
        assertThat(histogram.getTotalRows()).isEqualTo(10000);
    }

    @Test
    public void holdsAllOfTheNodesRowsEvenWhereTheMcvsCoverFewOfThem() {
        // GIVEN the hash of the staleness case above, seeing 50000 rows although only 10000 of them were measured.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), 50000);

        // THEN the bucket covers the rows the one projected MCV does not. The histogram describes the hash of this node,
        // and this node has 50000 rows regardless of how few of them we can name a value for.
        assertNotNull(histogram);
        assertThat(histogram.getBuckets()).singleElement()
                .satisfies(bucket -> assertThat(bucket.getCount()).isEqualTo(49984));
        assertThat(histogram.getTotalRows()).isEqualTo(50000);
    }

    @Test
    public void reportsOneRowFewerWhereTheFractionsAreInexact() {
        // GIVEN an argument covering 3% of its rows. Neither that fraction nor the 8% of the second argument are
        // representable exactly, so their product times the 10000 rows comes out as 23.999999999999996 rather than as
        // the 24 rows it is meant to be.
        final var firstArgument = varcharStatistic(0.02, 9800, Map.of("foo", 300L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN the combination is reported for 23 rows: truncating takes the inexactness with it, which can cost an MCV
        // one of its rows. That is the direction we prefer, as an MCV the optimizer underestimates only costs us the
        // skew we could have exploited.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 23L, // 3% * 8% * 10000 = 23.999999999999996
                hash(null, "x"), 16L // 2% * 8% * 10000
        ));
    }

    @Test
    public void truncatesEstimatesThatAreGenuinelyFractional() {
        // GIVEN two arguments whose combination covers 23.6 of the 10000 rows. Unlike the case above, that is not a whole
        // number of rows the fractions merely fail to represent; the combination really is expected in part of a row.
        // Both cases are truncated all the same, as there is no telling the two apart from the product alone.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 236L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN it is reported for the 23 rows we know it has rather than for the 24 that rounding would claim.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 23L // 10% * 2.36% * 10000 = 23.6
        ));
    }

    @Test
    public void multipliesTheFractionOfEveryAdditionalArgument() {
        // GIVEN
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));
        final var thirdArgument = varcharStatistic(0, 10000, Map.of("z", 2000L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument, thirdArgument), ROW_COUNT);

        // THEN all three fractions are multiplied, not just the first two.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x", "z"), 16L // 10% * 8% * 20% * 10000
        ));
    }

    @Test
    public void keepsOnlyTheMostFrequentCombinations() {
        // GIVEN the same arguments as above, which on their own produce nine combinations.
        context.getSessionVariable().setCelonisHashMcvsMultiArgLimitMcvs(3);
        final var firstArgument = varcharStatistic(0.02, 9800, Map.of("foo", 1000L, "bar", 500L));
        final var secondArgument = varcharStatistic(0.01, 9900, Map.of("x", 800L, "y", 300L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN the three most frequent combinations are kept, as if all nine had been ranked at once.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 80L,
                hash("bar", "x"), 40L,
                hash("foo", "y"), 30L
        ));
    }

    @Test
    public void projectsCombinationsOfAnArgumentThatIsOnlyKnownToBeNull() {
        // GIVEN a first argument with no MCVs at all, but with 2% of its rows NULL.
        final var firstArgument = varcharStatisticWithoutMcvs(0.02);
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN NULL carries the combination on its own.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash(null, "x"), 16L // 2% * 8% * 10000
        ));
    }

    @Test
    public void dropsCombinationsExpectedInLessThanAWholeRow() {
        // GIVEN a first argument that is NULL in only 10 of its 10000 rows.
        final var firstArgument = varcharStatistic(0.001, 9990, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN
        final var histogram = project(List.of(firstArgument, secondArgument), ROW_COUNT);

        // THEN the combination of that NULL with "x" is left out, as it is expected in 0.1% * 8% * 10000 = 0.8 rows, which
        // is not a row we can claim it has even though it is most of one.
        assertNotNull(histogram);
        assertThat(histogram.getMCV()).containsExactlyInAnyOrderEntriesOf(Map.of(
                hash("foo", "x"), 80L // 10% * 8% * 10000
        ));
    }

    @Test
    public void returnsNullWhenEveryCombinationIsExpectedInLessThanAWholeRow() {
        // GIVEN one value covering a tenth of the rows and one covering 8 of the 10000 of them.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 8L));

        // WHEN / THEN their combination is expected in 10% * 0.08% * 10000 = 0.8 rows, hence there is nothing to report.
        assertNull(project(List.of(firstArgument, secondArgument), ROW_COUNT));
    }

    @Test
    public void returnsNullWhenAnArgumentHasNoKnownValues() {
        // GIVEN a second argument with neither MCVs nor NULL rows.
        final var firstArgument = varcharStatistic(0.02, 9800, Map.of("foo", 1000L));
        final var secondArgument = varcharStatisticWithoutMcvs(0);

        // WHEN / THEN no combination can be completed.
        assertNull(project(List.of(firstArgument, secondArgument), ROW_COUNT));
    }

    @Test
    public void returnsNullForAnArgumentWhoseHistogramHasNoBuckets() {
        // GIVEN a second argument whose histogram holds MCVs but no buckets, as the ones projected through other
        // expressions do. Its total rows then cover no more than its MCVs, so the rows they were counted over are
        // unknown and could be many more than the histogram admits to.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, new Histogram(List.of(), Map.of("x", 800L)));

        // WHEN / THEN nothing is projected, rather than fractions of a population we cannot tell.
        assertNull(project(List.of(firstArgument, secondArgument), ROW_COUNT));
    }

    @Test
    public void returnsNullForAnArgumentWhoseStatisticsWereNeverCollected() {
        // GIVEN a second argument without statistics. Such a statistic reports every row as NULL, but that is the
        // placeholder it carries rather than something measured, so NULL is not a value we know of this argument.
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));

        // WHEN / THEN no combination can be completed, rather than one claiming that the argument is always NULL.
        assertNull(project(List.of(firstArgument, ColumnStatistic.unknown()), ROW_COUNT));
    }

    @Test
    public void returnsNullForNonVarcharArguments() {
        // GIVEN
        final var arguments = List.<ScalarOperator>of(
                new ColumnRefOperator(0, Type.VARCHAR, "str", true),
                new ColumnRefOperator(1, Type.INT, "num", true));
        final var statistics = List.of(
                varcharStatistic(0, 10000, Map.of("foo", 1000L)),
                varcharStatistic(0, 10000, Map.of("42", 800L)));

        // WHEN / THEN
        assertNull(project(FunctionSet.CELONIS_XX_HASH3_128_V3, arguments, statistics, ROW_COUNT));
    }

    @Test
    public void returnsNullForHashFunctionsWithoutAJavaImplementation() {
        // GIVEN
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));

        // WHEN / THEN
        assertNull(project(FunctionSet.CELONIS_XX_HASH3_128,
                varcharArguments(2), List.of(firstArgument, secondArgument), ROW_COUNT));
    }

    @Test
    public void returnsNullWhenTheSessionVariablesDisableTheProjection() {
        // GIVEN
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));
        final var arguments = List.of(firstArgument, secondArgument);

        // WHEN / THEN
        context.getSessionVariable().setEnableCelonisHashMcvs(false);
        assertNull(project(arguments, ROW_COUNT));

        context.getSessionVariable().setEnableCelonisHashMcvs(true);
        context.getSessionVariable().setEnableCelonisHashMcvsMultiArg(false);
        assertNull(project(arguments, ROW_COUNT));
    }

    @Test
    public void returnsNullForANonPositiveMcvLimitOrRowCount() {
        // GIVEN
        final var firstArgument = varcharStatistic(0, 10000, Map.of("foo", 1000L));
        final var secondArgument = varcharStatistic(0, 10000, Map.of("x", 800L));
        final var arguments = List.of(firstArgument, secondArgument);

        // WHEN / THEN
        context.getSessionVariable().setCelonisHashMcvsMultiArgLimitMcvs(0);
        assertNull(project(arguments, ROW_COUNT));

        context.getSessionVariable().setCelonisHashMcvsMultiArgLimitMcvs(20);
        assertNull(project(arguments, 0));
    }

    /**
     * A VARCHAR column statistic whose histogram holds the given MCVs and was collected over a table in which
     * {@code collectedNonNullRowCount} of the rows were not NULL. Together with the null fraction, that is what the
     * projection turns the absolute MCV counts into fractions with, so it decides what the MCVs mean.
     */
    private static ColumnStatistic varcharStatistic(double nullsFraction, long collectedNonNullRowCount,
                                                    Map<String, Long> mcv) {
        // The MCV rows are counted separately from the buckets, so the bucket holds whatever the MCVs do not cover. This
        // mirrors the single placeholder bucket the collect job stores for VARCHAR columns.
        final var mcvRowCount = mcv.values().stream().mapToLong(Long::longValue).sum();
        final var bucket = new Bucket(POSITIVE_INFINITY, POSITIVE_INFINITY,
                Math.max(0, collectedNonNullRowCount - mcvRowCount), 0L);

        return varcharStatistic(nullsFraction, new Histogram(List.of(bucket), mcv));
    }

    /**
     * A VARCHAR column statistic with the given null fraction and no histogram, hence no known values but NULL.
     */
    private static ColumnStatistic varcharStatisticWithoutMcvs(double nullsFraction) {
        return varcharStatistic(nullsFraction, null);
    }

    private static ColumnStatistic varcharStatistic(double nullsFraction, @Nullable Histogram histogram) {
        final var builder = ColumnStatistic.builder() //
                .setMinValue(NEGATIVE_INFINITY) //
                .setMaxValue(POSITIVE_INFINITY) //
                .setDistinctValuesCount(1000) //
                .setNullsFraction(nullsFraction) //
                .setAverageRowSize(10);

        if (histogram != null) {
            builder.setHistogram(histogram);
        }

        return builder.build();
    }

    private static List<ScalarOperator> varcharArguments(int count) {
        final var arguments = new ArrayList<ScalarOperator>(count);
        for (var index = 0; index < count; index++) {
            arguments.add(new ColumnRefOperator(index, Type.VARCHAR, "col" + index, true));
        }
        return arguments;
    }

    private static Histogram projectSingleArg(ColumnStatistic columnStatistic, double rowCount) {
        return projectSingleArg(FunctionSet.CELONIS_XX_HASH3_128_V3, varcharArguments(1).get(0), columnStatistic,
                rowCount);
    }

    private static Histogram projectSingleArg(String functionName, ScalarOperator argument,
                                              ColumnStatistic columnStatistic, double rowCount) {
        final var callOperator = new CallOperator(functionName, Type.LARGEINT, List.of(argument));
        return CelonisHashMcvProjector.projectSingleArgHash(callOperator, columnStatistic, rowCount);
    }

    private static Histogram project(List<ColumnStatistic> childrenColumnStatistics, double rowCount) {
        return project(FunctionSet.CELONIS_XX_HASH3_128_V3, varcharArguments(childrenColumnStatistics.size()),
                childrenColumnStatistics, rowCount);
    }

    private static Histogram project(String functionName, List<ScalarOperator> arguments,
                                     List<ColumnStatistic> childrenColumnStatistics, double rowCount) {
        final var callOperator = new CallOperator(functionName, Type.LARGEINT, arguments);
        try {
            return CelonisHashMcvProjector.projectMultiArgHash(callOperator, childrenColumnStatistics, rowCount);
        } catch (CelonisHashCalculationException exception) {
            throw new AssertionError("Test value combination could not be hashed", exception);
        }
    }

    private static String hash(String... values) {
        try {
            return new XXHASH3128V3().compute(values).toString();
        } catch (CelonisHashCalculationException exception) {
            throw new AssertionError("Test values could not be hashed", exception);
        }
    }
}
