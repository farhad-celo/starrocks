package com.celonis.invoicechecker;

import org.json.JSONObject;
import org.junit.jupiter.api.Test;
import org.skyscreamer.jsonassert.JSONAssert;
import org.skyscreamer.jsonassert.JSONCompareMode;

import static org.junit.jupiter.api.Assertions.*;

class ReferenceMatcherTest {
    @Test
    void testReference() {
        ReferenceMatcher.Reference reference = new ReferenceMatcher.Reference("10", "bar");
        reference.setModifiedReference(("fooBar"));
        JSONAssert.assertEquals("{\"id\": \"10\", \"ref\": \"bar\"}", new JSONObject(reference.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testCluster() {
        ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "foo");
        ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "bar");
        Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
        cluster.getClusterObjects().add(reference1);
        cluster.getClusterObjects().add(reference2);
        JSONAssert.assertEquals("{ \"c\": [{\"id\": \"10\", \"ref\": \"foo\"}, {\"id\": \"20\", \"ref\": \"bar\"}]}",
                new JSONObject(cluster.toJsonString()), JSONCompareMode.STRICT);
    }

    @Test
    void testFullMatchAfterModification() {
        ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   FOOBAR    `   ");
        ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobar @#");
        Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
        cluster.getClusterObjects().add(reference1);
        cluster.getClusterObjects().add(reference2);
        ReferenceMatcher matcher = new ReferenceMatcher();
        assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"   FOOBAR    `   \"}, {\"id\": \"20\", \"ref\": \"   foobar @#\"}]}"},
                matcher.process(cluster.toJsonString()));
    }

    @Test
    void testNotMatchForEmptyStr() {
        ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "      ");
        ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobar @#");
        Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
        cluster.getClusterObjects().add(reference1);
        cluster.getClusterObjects().add(reference2);
        ReferenceMatcher matcher = new ReferenceMatcher();
        assertEquals(0, matcher.process(cluster.toJsonString()).length);
    }

   @Test
   void testFullMatchForSubstr() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   FOOBAR    `   ");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobartar @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{ "{ \"c\": [{\"id\": \"10\", \"ref\": \"   FOOBAR    `   \"}, {\"id\": \"20\", \"ref\": \"   foobartar @#\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testNotMatchForLengthDiffGreaterThanThree() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "      fo");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobar @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertEquals(0, matcher.process( cluster.toJsonString()).length);
   }

   @Test
   void testFullMatchAfterTranslation() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   FOOBARtar8    `   ");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobartarb @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"   FOOBARtar8    `   \"}, {\"id\": \"20\", \"ref\": \"   foobartarb @#\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testFullMatchAfterTwoTurners() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   FOOABRtar8    `   ");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobartar8 @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"   FOOABRtar8    `   \"}, {\"id\": \"20\", \"ref\": \"   foobartar8 @#\"}]}"}
               ,matcher.process(cluster.toJsonString()));
   }

   @Test
   void testNotMatchAfterThreeTurners() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   foaobr");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobar @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertEquals(0, matcher.process(cluster.toJsonString()).length);
   }

   @Test
   void testNotMatchForShortStrs() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   fo");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   fa @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertEquals(0, matcher.process(cluster.toJsonString()).length);
   }

   @Test
   void testFullMatchForTwoSkips() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "117090010");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "11709000101");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"117090010\"}, {\"id\": \"20\", \"ref\": \"11709000101\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testFullMatchForOrderedSubStr() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   FOOTAR8    `   ");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobartar8 @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"   FOOTAR8    `   \"}, {\"id\": \"20\", \"ref\": \"   foobartar8 @#\"}]}"},
               matcher.process(cluster.toJsonString()));
   }

   @Test
   void testNotMatchForNotOrderedSubStr() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "   footarb");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "   foobartar @#");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertEquals(0, matcher.process(cluster.toJsonString()).length);
   }

   @Test
   void testNotMatchWhenTooManySkips() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "11709000101000");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "117090010");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertEquals(0, matcher.process(cluster.toJsonString()).length);
   }

   @Test
   void testShouldMatchWhenThreeSkips() {
       ReferenceMatcher.Reference reference1 = new ReferenceMatcher.Reference("10", "117090001010");
       ReferenceMatcher.Reference reference2 = new ReferenceMatcher.Reference("20", "117090010");
       Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
       cluster.getClusterObjects().add(reference1);
       cluster.getClusterObjects().add(reference2);
       ReferenceMatcher matcher = new ReferenceMatcher();
       assertArrayEquals(new String[]{"{ \"c\": [{\"id\": \"10\", \"ref\": \"117090001010\"}, {\"id\": \"20\", \"ref\": \"117090010\"}]}"},
               matcher.process(cluster.toJsonString()));
   }
}