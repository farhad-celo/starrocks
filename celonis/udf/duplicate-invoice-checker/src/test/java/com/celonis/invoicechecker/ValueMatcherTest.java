package com.celonis.invoicechecker;

import org.json.JSONObject;
import org.junit.jupiter.api.Test;
import org.skyscreamer.jsonassert.JSONAssert;
import org.skyscreamer.jsonassert.JSONCompareMode;

import static org.junit.jupiter.api.Assertions.*;

class ValueMatcherTest {
    @Test
    void testValue() {
        ValueMatcher.Value value = new ValueMatcher.Value("10", 42.5);
        value.setNormalizedValueStr("4250");
        JSONAssert.assertEquals("{\"id\": \"10\", \"val\": 42.5}", new JSONObject(value.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testCluster() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 42.5);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 36.8);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        JSONAssert.assertEquals("{ \"c\": [{\"id\": \"10\", \"val\": 42.5}, {\"id\": \"20\", \"val\": 36.8}]}",
                new JSONObject(cluster.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testFullMatchWhenValueEquals() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 42.5);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 42.5);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 42.5}, {\"id\": \"20\", \"val\": 42.5}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testFullMatchWhenSmallValueDiff() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 102.5);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 99.5);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 102.5}, {\"id\": \"20\", \"val\": 99.5}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testFullMatchWhenDigitsAreDifferent() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 1111.5);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 2222.5);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    void testFullMatchWhenFourDigitsTurners() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 1234);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 3142);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 1234.0}, {\"id\": \"20\", \"val\": 3142.0}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testFullMatchWhenFourDigitsTurners2() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 1450760.11);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 1050714.16);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 1450760.11}, {\"id\": \"20\", \"val\": 1050714.16}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testFullMatchWhenValueDiffBy80() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 4644323.18);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 4644403.18);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 4644323.18}, {\"id\": \"20\", \"val\": 4644403.18}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testFullMatchWhenOnlyConsideringTwoDecimals() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 199.125);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 222.222);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"val\": 199.125}, {\"id\": \"20\", \"val\": 222.222}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testNotMatchWhenLargeValueDiff() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 80.5);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 190.5);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testNotMatchWhenFiveTurners() {
        ValueMatcher.Value value1 = new ValueMatcher.Value("10", 12345);
        ValueMatcher.Value value2 = new ValueMatcher.Value("20", 23456);
        Cluster<ValueMatcher.Value> cluster = new Cluster<>();
        cluster.getClusterObjects().add(value1);
        cluster.getClusterObjects().add(value2);
        ValueMatcher matcher = new ValueMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }
}