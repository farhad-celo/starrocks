package com.celonis.invoicechecker;

import lombok.var;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;
import org.skyscreamer.jsonassert.JSONAssert;
import org.skyscreamer.jsonassert.JSONCompareMode;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.util.Map;
import java.util.HashMap;
import java.util.TreeMap;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;


import static org.junit.jupiter.api.Assertions.*;

class ReferenceMatcherStressTest {

    @ParameterizedTest
    @CsvSource({"1000,Graph", "1000,Pairwise", "1000,Index",
            "3000,Graph", "3000,Pairwise", "3000,Index",
            "10000,Graph", "10000,Pairwise", "10000,Index",
            "30000,Graph", "30000,Pairwise", "30000,Index"})
    void testGroupSize(int groupSize, String matchAlgorithm) {
        String line = "";
        String splitBy = "\t";
        try {
            //parsing a CSV file into BufferedReader class constructor
            BufferedReader br = new BufferedReader(
                    new FileReader("src/test/java/com/celonis/invoicechecker/fake_data_with_duplicates_"
                            + groupSize + "_group_size.csv"));
            int index = 0;
            Map<String, Cluster<ReferenceMatcher.Reference>> referenceClusters = new TreeMap<>();
            while ((line = br.readLine()) != null) {
                if (index == 0) {
                    index++;
                    continue;
                }
                String[] results = line.split(splitBy);
                String indexKey = results[2] + "\t" + results[4] + "\t" + results[5];
                long rowId = Long.valueOf(results[0]);
                String reference = results[3];
                if (referenceClusters.containsKey(indexKey)) {
                    referenceClusters.get(indexKey).getClusterObjects().add(new ReferenceMatcher.Reference(
                            String.valueOf(rowId), reference));
                } else {
                    Cluster<ReferenceMatcher.Reference> cluster = new Cluster<>();
                    cluster.getClusterObjects().add(new ReferenceMatcher.Reference(String.valueOf(rowId), reference));
                    referenceClusters.put(indexKey, cluster);
                }
                index ++;
            }
            int maxGroupSize = 0;
            String maxGroupSizeKey = "";
            for (String key : referenceClusters.keySet()) {
                if (referenceClusters.get(key).getClusterObjects().size() > maxGroupSize) {
                    maxGroupSizeKey = key;
                    maxGroupSize = referenceClusters.get(key).getClusterObjects().size();
                }
            }
            System.out.println("max group size: " + maxGroupSize);
            System.out.println("max group size key: " + maxGroupSizeKey);
            String inputStr = "";
            {
                long start = System.currentTimeMillis();
                inputStr = referenceClusters.get(maxGroupSizeKey).toJsonString();
                long end = System.currentTimeMillis();
                System.out.println("Construct input str takes " + (end - start) + " ms");
            }
            //System.out.println("Input json str: " + inputStr);
            System.out.println("Input json str size: " + inputStr.length());
            ReferenceMatcher matcher = new ReferenceMatcher(matchAlgorithm);
            String[] matchedStrs;
            {
                long start = System.currentTimeMillis();
                matchedStrs = matcher.process(inputStr);
                long end = System.currentTimeMillis();
                System.out.println("Evaluate takes " + (end - start) + " ms");
            }
            System.out.println("Match times: " + matcher.matchTimes);
            //JSONObject jsonObject = new JSONObject(matchedStr);
            System.out.println("Matched clusters: " + matchedStrs.length);
            int countItems = 0;
            for (int i = 0; i < matchedStrs.length; i++) {
                JSONObject subObject = new JSONObject(matchedStrs[i]);
                int length = subObject.getJSONArray("c").length();
                // System.out.println("Cluster length: " + length);
                // System.out.println("Cluster objects: " + subObject);
                countItems += length;
            }
            System.out.println("Total length: " + countItems);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
