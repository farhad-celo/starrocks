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
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.google.common.base.Ticker;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import mockit.Expectations;
import mockit.Mocked;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

public class CachingUnityCatalogClientTest {

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

    @Test
    public void testGetTableCachesByFullName(@Mocked UnityCatalogApi delegate) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/orders");

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                result = info;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        Assertions.assertSame(info, client.getTable("main.sales.orders"));
        Assertions.assertSame(info, client.getTable("main.sales.orders"));
    }

    @Test
    public void testTableExistsShortCircuitsWhenTableInfoCached(@Mocked UnityCatalogApi delegate) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/orders");

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                result = info;
                times = 1;
                delegate.tableExists(anyString);
                times = 0;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        client.getTable("main.sales.orders");
        Assertions.assertTrue(client.tableExists("main.sales.orders"));
    }

    @Test
    public void testTableExistsDelegatesWhenNotCached(@Mocked UnityCatalogApi delegate) {
        new Expectations() {
            {
                delegate.tableExists("main.sales.missing");
                result = false;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertFalse(client.tableExists("main.sales.missing"));
    }

    @Test
    public void testTableExistsDoesNotNegativeCache(@Mocked UnityCatalogApi delegate) {
        // v1 explicitly skips negative caching: two calls -> two delegate hits.
        new Expectations() {
            {
                delegate.tableExists("main.sales.missing");
                result = false;
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertFalse(client.tableExists("main.sales.missing"));
        Assertions.assertFalse(client.tableExists("main.sales.missing"));
    }

    @Test
    public void testCredentialsCacheHitWhenWellBeforeExpiry(@Mocked UnityCatalogApi delegate) {
        GenerateTemporaryTableCredentialResponse creds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(10_000_000L);
        AtomicLong wallClock = new AtomicLong(1_000L);
        new Expectations() {
            {
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                result = creds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), wallClock);
        Assertions.assertSame(creds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(creds, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testCredentialsBypassedWhenInsideSafetyMargin(@Mocked UnityCatalogApi delegate) {
        GenerateTemporaryTableCredentialResponse first = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(10_000L);
        GenerateTemporaryTableCredentialResponse second = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(200_000L);

        AtomicLong wallClock = new AtomicLong(1_000L);
        new Expectations() {
            {
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                returns(first, second);
                times = 2;
            }
        };

        // safety margin 60s => creds that expire at 10s are always "near expiry".
        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), wallClock);
        Assertions.assertSame(first, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(second, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testCredentialsWithoutExpirationTimeAreNotCached(@Mocked UnityCatalogApi delegate) {
        GenerateTemporaryTableCredentialResponse first = new GenerateTemporaryTableCredentialResponse();
        GenerateTemporaryTableCredentialResponse second = new GenerateTemporaryTableCredentialResponse();

        new Expectations() {
            {
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                returns(first, second);
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        Assertions.assertSame(first, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(second, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testTtlExpiryReloadsEntry(@Mocked UnityCatalogApi delegate) {
        TableInfo info1 = new TableInfo().setFullName("main.sales.orders");
        TableInfo info2 = new TableInfo().setFullName("main.sales.orders");

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                returns(info1, info2);
                times = 2;
            }
        };

        AdvanceableTicker ticker = new AdvanceableTicker();
        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60), ticker, new AtomicLong());

        Assertions.assertSame(info1, client.getTable("main.sales.orders"));
        ticker.advance(61, TimeUnit.SECONDS);
        Assertions.assertSame(info2, client.getTable("main.sales.orders"));
    }

    @Test
    public void testTtlExpiryReloadsCredentialEntry(@Mocked UnityCatalogApi delegate) {
        GenerateTemporaryTableCredentialResponse first = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);
        GenerateTemporaryTableCredentialResponse second = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                returns(first, second);
                times = 2;
            }
        };

        AdvanceableTicker ticker = new AdvanceableTicker();
        CachingUnityCatalogClient client = newClient(delegate, propsWith(2, 60), ticker, new AtomicLong());

        Assertions.assertSame(first, client.getTemporaryTableCredentials("abc-123", "READ"));
        ticker.advance(3, TimeUnit.SECONDS);
        Assertions.assertSame(second, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testInvalidateClearsTableInfoButKeepsCredentials(@Mocked UnityCatalogApi delegate) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/orders");

        GenerateTemporaryTableCredentialResponse creds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                result = info;
                times = 2;
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                result = creds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        client.getTable("main.sales.orders");
        client.getTemporaryTableCredentials("abc-123", "READ");

        client.invalidate("main.sales.orders");

        client.getTable("main.sales.orders");
        Assertions.assertSame(creds, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testCredentialsCachedPerOperation(@Mocked UnityCatalogApi delegate) {
        GenerateTemporaryTableCredentialResponse readCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);
        GenerateTemporaryTableCredentialResponse writeCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                // Each (tableId, operation) is its own request identity. Two distinct
                // operations on the same tableId must not alias each other in the cache.
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                result = readCreds;
                times = 1;
                delegate.getTemporaryTableCredentials("abc-123", "READ_WRITE");
                result = writeCreds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());

        Assertions.assertSame(readCreds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(writeCreds, client.getTemporaryTableCredentials("abc-123", "READ_WRITE"));
        // Repeats hit the per-operation entry, not each other.
        Assertions.assertSame(readCreds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(writeCreds, client.getTemporaryTableCredentials("abc-123", "READ_WRITE"));
    }

    @Test
    public void testInvalidateKeepsCredentialsForEveryOperation(@Mocked UnityCatalogApi delegate) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/orders");

        GenerateTemporaryTableCredentialResponse readCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);
        GenerateTemporaryTableCredentialResponse writeCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                result = info;
                times = 1;
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                result = readCreds;
                times = 1;
                delegate.getTemporaryTableCredentials("abc-123", "READ_WRITE");
                result = writeCreds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60),
                new AdvanceableTicker(), new AtomicLong());
        client.getTable("main.sales.orders");
        client.getTemporaryTableCredentials("abc-123", "READ");
        client.getTemporaryTableCredentials("abc-123", "READ_WRITE");

        client.invalidate("main.sales.orders");

        Assertions.assertSame(readCreds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(writeCreds, client.getTemporaryTableCredentials("abc-123", "READ_WRITE"));
    }

    @Test
    public void testZeroTtlBypassesMetadataAndCredentialsCache(@Mocked UnityCatalogApi delegate) {
        SchemaInfo s = new SchemaInfo().setName("sales");
        GenerateTemporaryTableCredentialResponse firstCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);
        GenerateTemporaryTableCredentialResponse secondCreds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.listSchemas("main");
                result = ImmutableList.of(s);
                times = 2;
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                returns(firstCreds, secondCreds);
                times = 2;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(0, 60),
                new AdvanceableTicker(), new AtomicLong());
        client.listSchemas("main");
        client.listSchemas("main");
        Assertions.assertSame(firstCreds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(secondCreds, client.getTemporaryTableCredentials("abc-123", "READ"));
    }

    @Test
    public void testDisabledMetadataCacheStillCachesCredentials(@Mocked UnityCatalogApi delegate) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/orders");
        GenerateTemporaryTableCredentialResponse creds = new GenerateTemporaryTableCredentialResponse()
                .setExpirationTime(Long.MAX_VALUE);

        new Expectations() {
            {
                delegate.getTable("main.sales.orders");
                result = info;
                times = 2;
                delegate.getTemporaryTableCredentials("abc-123", "READ");
                result = creds;
                times = 1;
            }
        };

        CachingUnityCatalogClient client = newClient(delegate, propsWith(60, 60, false),
                new AdvanceableTicker(), new AtomicLong());
        client.getTable("main.sales.orders");
        client.getTable("main.sales.orders");
        Assertions.assertSame(creds, client.getTemporaryTableCredentials("abc-123", "READ"));
        Assertions.assertSame(creds, client.getTemporaryTableCredentials("abc-123", "READ"));
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
