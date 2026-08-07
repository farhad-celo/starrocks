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

package com.starrocks.connector.delta.unity;

import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.google.common.base.Ticker;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import mockit.Expectations;
import mockit.Mocked;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Stream;

public class CachingUnityCatalogClientTest {

    private static final String TABLE_ID = "11111111-1111-1111-1111-111111111111";

    private static UnityCatalogProperties propsWith(long ttlSec, long credentialsSafetyMarginSec) {
        return propsWith(ttlSec, credentialsSafetyMarginSec, true);
    }

    private static UnityCatalogProperties propsWith(long ttlSec, long credentialsSafetyMarginSec,
                                                    boolean metadataCacheEnabled) {
        return new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.cache.enabled", Boolean.toString(metadataCacheEnabled),
                "unity.catalog.cache.ttl-sec", Long.toString(ttlSec),
                "unity.catalog.cache.credentials.safety-margin-sec", Long.toString(credentialsSafetyMarginSec)));
    }

    /** Manually-advanceable ticker; avoids pulling in guava-testlib. */
    private static final class AdvanceableTicker extends Ticker {
        private final AtomicLong nanos = new AtomicLong();

        @Override
        public long read() {
            return nanos.get();
        }

        void advance(long duration, TimeUnit unit) {
            nanos.addAndGet(unit.toNanos(duration));
        }
    }

    private static CachingUnityCatalogClient newClient(UnityCatalogApi delegate,
                                                       UnityCatalogProperties props,
                                                       Ticker ticker,
                                                       AtomicLong clockMillis) {
        return new CachingUnityCatalogClient(delegate, props, ticker, clockMillis::get);
    }

    @Test
    public void testListSchemasCachesAcrossCalls(@Mocked UnityCatalogApi delegate) {
        SchemaInfo s = new SchemaInfo().setName("sales");
        new Expectations() {
            {
                delegate.listSchemas("main");
                result = ImmutableList.of(s);
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        List<SchemaInfo> first = client.listSchemas("main");
        List<SchemaInfo> second = client.listSchemas("main");
        Assertions.assertEquals(1, first.size());
        Assertions.assertSame(first, second, "second call must be a cache hit");
    }

    @Test
    public void testListTablesCachedPerSchema(@Mocked UnityCatalogApi delegate) {
        TableInfo t = new TableInfo().setName("orders").setDataSourceFormat(DataSourceFormat.DELTA);
        new Expectations() {
            {
                delegate.listTables("main", "sales");
                result = ImmutableList.of(t);
                times = 1;
                delegate.listTables("main", "marketing");
                result = ImmutableList.<TableInfo>of();
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        client.listTables("main", "sales");
        client.listTables("main", "sales");
        client.listTables("main", "marketing");
        client.listTables("main", "marketing");
    }

    @ParameterizedTest
    @CsvSource({"main.sales.orders, true", "main.sales.missing, false"})
    public void testTableExistsAlwaysDelegates(String fullName, boolean exists, @Mocked UnityCatalogApi delegate) {
        // v1 has no cache to short-circuit on and does not negative-cache: every call reaches the delegate.
        new Expectations() {
            {
                delegate.tableExists(fullName);
                result = exists;
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertEquals(exists, client.tableExists(fullName));
        Assertions.assertEquals(exists, client.tableExists(fullName));
    }

    @Test
    public void testLoadTableAlwaysDelegates(@Mocked UnityCatalogApi delegate) {
        DeltaLoadTableResponse first = UnityDeltaModelFixtures.loadResponse("s3://bucket/orders", 1L, null);
        DeltaLoadTableResponse second = UnityDeltaModelFixtures.loadResponse("s3://bucket/orders", 2L, null);
        new Expectations() {
            {
                delegate.loadTable("main", "sales", "orders");
                returns(first, second);
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertSame(first, client.loadTable("main", "sales", "orders"));
        Assertions.assertSame(second, client.loadTable("main", "sales", "orders"));
    }

    private static Stream<Arguments> credentialExpiryCases() {
        return Stream.of(
                // Well before expiry: cached, delegate hit once.
                Arguments.of(UnityDeltaModelFixtures.awsCredsResponse(10_000_000L),
                        UnityDeltaModelFixtures.awsCredsResponse(10_000_000L), 1_000L, true),
                // Inside the 60s safety margin: re-vended on every call.
                Arguments.of(UnityDeltaModelFixtures.awsCredsResponse(10_000L),
                        UnityDeltaModelFixtures.awsCredsResponse(200_000L), 1_000L, false),
                // Earliest expiry across credentials wins, so the response is near-expiry and not cached.
                Arguments.of(UnityDeltaModelFixtures.credsResponse(
                                UnityDeltaModelFixtures.credential("s3://bucket/a", UnityDeltaModelFixtures.awsConfig(), 10_000_000L),
                                UnityDeltaModelFixtures.credential("s3://bucket/b", UnityDeltaModelFixtures.awsConfig(), 10_000L)),
                        UnityDeltaModelFixtures.awsCredsResponse(10_000_000L), 1_000L, false));
    }

    @ParameterizedTest
    @MethodSource("credentialExpiryCases")
    public void testCredentialExpiryDrivesCaching(DeltaCredentialsResponse first, DeltaCredentialsResponse second,
                                                  long wallClockMillis, boolean expectCached,
                                                  @Mocked UnityCatalogApi delegate) {
        new Expectations() {
            {
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                if (expectCached) {
                    result = first;
                    times = 1;
                } else {
                    returns(first, second);
                    times = 2;
                }
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong(wallClockMillis));
        Assertions.assertSame(first, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(expectCached ? first : second,
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
    }

    @Test
    public void testTtlExpiryReloadsCredentialEntry(@Mocked UnityCatalogApi delegate) {
        DeltaCredentialsResponse first = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);
        DeltaCredentialsResponse second = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                returns(first, second);
                times = 2;
            }
        };

        AdvanceableTicker ticker = new AdvanceableTicker();
        CachingUnityCatalogClient client = newClient(delegate, propsWith(2, 60), ticker, new AtomicLong());

        Assertions.assertSame(first, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        ticker.advance(3, TimeUnit.SECONDS);
        Assertions.assertSame(second, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
    }

    @Test
    public void testCredentialsCachedPerOperation(@Mocked UnityCatalogApi delegate) {
        DeltaCredentialsResponse readCreds = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);
        DeltaCredentialsResponse writeCreds = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);

        new Expectations() {
            {
                // Distinct operations on the same table must not alias each other in the cache.
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = readCreds;
                times = 1;
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ_WRITE");
                result = writeCreds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        Assertions.assertSame(readCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(writeCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ_WRITE"));
        // Repeats hit the per-operation entry, not each other.
        Assertions.assertSame(readCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(writeCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ_WRITE"));
    }

    @Test
    public void testCredentialsCachedPerTableId(@Mocked UnityCatalogApi delegate) {
        // Same three-part name, different Delta table UUID (a drop/recreate): must not alias.
        String recreatedId = "22222222-2222-2222-2222-222222222222";
        DeltaCredentialsResponse original = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);
        DeltaCredentialsResponse recreated = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = original;
                times = 1;
                delegate.getTableCredentials("main", "sales", "orders", recreatedId, "READ");
                result = recreated;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        Assertions.assertSame(original, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(recreated, client.getTableCredentials("main", "sales", "orders", recreatedId, "READ"));
        Assertions.assertSame(original, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(recreated, client.getTableCredentials("main", "sales", "orders", recreatedId, "READ"));
    }

    @Test
    public void testZeroTtlBypassesMetadataAndCredentialsCache(@Mocked UnityCatalogApi delegate) {
        SchemaInfo s = new SchemaInfo().setName("sales");
        DeltaCredentialsResponse firstCreds = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);
        DeltaCredentialsResponse secondCreds = UnityDeltaModelFixtures.awsCredsResponse(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.listSchemas("main");
                result = ImmutableList.of(s);
                times = 2;
                delegate.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                returns(firstCreds, secondCreds);
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(0, 60),
                new AdvanceableTicker(), new AtomicLong());
        client.listSchemas("main");
        client.listSchemas("main");
        Assertions.assertSame(firstCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
        Assertions.assertSame(secondCreds, client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ"));
    }

    @Test
    public void testGetMetastoreSummaryDelegatesEveryCall(@Mocked UnityCatalogApi delegate) {
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");
        new Expectations() {
            {
                delegate.getMetastoreSummary();
                result = summary;
                times = 3;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertSame(summary, client.getMetastoreSummary());
        Assertions.assertSame(summary, client.getMetastoreSummary());
        Assertions.assertSame(summary, client.getMetastoreSummary());
    }
}
