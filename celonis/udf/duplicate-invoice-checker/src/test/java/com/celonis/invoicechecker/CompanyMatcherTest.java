package com.celonis.invoicechecker;

import org.json.JSONObject;
import org.junit.jupiter.api.Test;
import org.skyscreamer.jsonassert.JSONAssert;
import org.skyscreamer.jsonassert.JSONCompareMode;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

class CompanyMatcherTest {
    @Test
    void testCompany() {
        CompanyMatcher.Company company = new CompanyMatcher.Company("10", "foo");
        company.setModifiedVendorName("fooBar");
        JSONAssert.assertEquals("{\"id\": \"10\", \"vendor_name\": \"foo\"}",
                new JSONObject(company.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testCluster() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "foo");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "bar");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        JSONAssert.assertEquals("{ \"c\": [{\"id\": \"10\", \"vendor_name\": \"foo\"}, {\"id\": \"20\", \"vendor_name\": \"bar\"}]}",
                new JSONObject(cluster.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testEvaluateWithMatch() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "   FOO    `LLC   ");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "   foo @#Inc");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"vendor_name\": \"   FOO    `LLC   \"}, {\"id\": \"20\", \"vendor_name\": \"   foo @#Inc\"}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testEvaluateWithMatch2() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "Wal-Mart Transportation, LLC");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "Walmart Inc.");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testEvaluateWithoutMatch3() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "Wal-Mart Stores East, LP");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "Walmart Inc");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testEvaluateWithoutMatch1() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "   FOO    `LLC   ");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "   bar @#Inc");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testEvaluateWithoutMatch2() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "C2O PURE COCONUT WATER LLC");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "Dyla LLC");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testSpecialCase() {
        CompanyMatcher.Company company1 = new CompanyMatcher.Company("10", "Schenker France SAS");
        CompanyMatcher.Company company2 = new CompanyMatcher.Company("20", "Schenker S.A.");
        Cluster<CompanyMatcher.Company> cluster = new Cluster<>();
        cluster.getClusterObjects().add(company1);
        cluster.getClusterObjects().add(company2);
        CompanyMatcher matcher = new CompanyMatcher();
        assertEquals(1, matcher.process(cluster.toJsonString()).length);
    }
}