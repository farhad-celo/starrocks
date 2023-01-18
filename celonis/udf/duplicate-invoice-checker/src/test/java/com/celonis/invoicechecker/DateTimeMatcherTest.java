package com.celonis.invoicechecker;

import org.json.JSONObject;
import org.junit.jupiter.api.Test;
import org.skyscreamer.jsonassert.JSONAssert;
import org.skyscreamer.jsonassert.JSONCompareMode;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;

import static org.junit.jupiter.api.Assertions.*;

class DateTimeMatcherTest {
    @Test
    void testDateTime() throws ParseException {
        DateTimeMatcher.DateTime dateTime = new DateTimeMatcher.DateTime("10", "2020-01-02 12:01:01");
        SimpleDateFormat simpleDateFormat = new SimpleDateFormat("yyyy-mm-dd HH:mm:ss");
        Date parsedDate = simpleDateFormat.parse(dateTime.getDateTimeStr());
        dateTime.setDateTime(parsedDate);

        JSONAssert.assertEquals("{\"id\": \"10\", \"date_time\": \"2020-01-02 12:01:01\"}",
                new JSONObject(dateTime.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testCluster() {
        DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-01-02 12:01:01");
        DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-01-03 12:01:01");
        Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
        cluster.getClusterObjects().add(dateTime1);
        cluster.getClusterObjects().add(dateTime2);
        JSONAssert.assertEquals("{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-01-02 12:01:01\"}, "
                + "{\"id\": \"20\", \"date_time\": \"2020-01-03 12:01:01\"}]}",
                new JSONObject(cluster.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testIllegalDatesAreIgnored() {
        DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2045-01-03 04:01:01");
        DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "1950-01-03 12:01:01");
        DateTimeMatcher.DateTime dateTime3 = new DateTimeMatcher.DateTime("20", "1950-01-03 foobar");
        Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
        cluster.getClusterObjects().add(dateTime1);
        cluster.getClusterObjects().add(dateTime2);
        cluster.getClusterObjects().add(dateTime3);
        DateTimeMatcher matcher = new DateTimeMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

    @Test
    void testFullMatchWhenDatesEqual() {
        DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-01-03 04:01:01");
        DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-01-03 12:01:01");
        Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
        cluster.getClusterObjects().add(dateTime1);
        cluster.getClusterObjects().add(dateTime2);
        DateTimeMatcher matcher = new DateTimeMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-01-03 04:01:01\"}, " +
                "{\"id\": \"20\", \"date_time\": \"2020-01-03 12:01:01\"}]}"},
                matcher.process(cluster.toJsonString()));
    }

   @Test
   void testFullMatchWhenDatesAreClose() {
       DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-01-10 04:01:01");
       DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-01-03 12:01:01");
       Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
       cluster.getClusterObjects().add(dateTime1);
       cluster.getClusterObjects().add(dateTime2);
       DateTimeMatcher matcher = new DateTimeMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-01-10 04:01:01\"}, " +
               "{\"id\": \"20\", \"date_time\": \"2020-01-03 12:01:01\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testNotMatchWhenDatesAreNotClose() {
       DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-01-11 04:01:01");
       DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-01-03 12:01:01");
       Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
       cluster.getClusterObjects().add(dateTime1);
       cluster.getClusterObjects().add(dateTime2);
       DateTimeMatcher matcher = new DateTimeMatcher();
       assertEquals(0, matcher.process(cluster.toJsonString()).length);
   }

   @Test
   void testFullMatchWhenDayAndMonthSwapped() {
       DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-01-10 04:01:01");
       DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-10-01 13:01:01");
       Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
       cluster.getClusterObjects().add(dateTime1);
       cluster.getClusterObjects().add(dateTime2);
       DateTimeMatcher matcher = new DateTimeMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-01-10 04:01:01\"}, " +
               "{\"id\": \"20\", \"date_time\": \"2020-10-01 13:01:01\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testFullMatchForConfusingMonths1() {
       DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-06-10 04:01:01");
       DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-07-10 13:01:01");
       Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
       cluster.getClusterObjects().add(dateTime1);
       cluster.getClusterObjects().add(dateTime2);
       DateTimeMatcher matcher = new DateTimeMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-06-10 04:01:01\"}, " +
               "{\"id\": \"20\", \"date_time\": \"2020-07-10 13:01:01\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testFullMatchForConfusingMonths2() {
       DateTimeMatcher.DateTime dateTime1 = new DateTimeMatcher.DateTime("10", "2020-09-10 04:01:01");
       DateTimeMatcher.DateTime dateTime2 = new DateTimeMatcher.DateTime("20", "2020-10-10 13:01:01");
       Cluster<DateTimeMatcher.DateTime> cluster = new Cluster<>();
       cluster.getClusterObjects().add(dateTime1);
       cluster.getClusterObjects().add(dateTime2);
       DateTimeMatcher matcher = new DateTimeMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"date_time\": \"2020-09-10 04:01:01\"}, " +
               "{\"id\": \"20\", \"date_time\": \"2020-10-10 13:01:01\"}]}"},
               matcher.process(cluster.toJsonString()));
   }
}