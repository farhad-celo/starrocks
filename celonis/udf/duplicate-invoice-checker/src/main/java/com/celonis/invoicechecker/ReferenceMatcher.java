package com.celonis.invoicechecker;

import java.sql.Ref;
import java.util.*;

import org.json.*;
import lombok.RequiredArgsConstructor;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import lombok.Getter;
import lombok.Setter;

public class ReferenceMatcher {
    public ReferenceMatcher(String matchAlgorithm) {
        this.matchAlgorithm = matchAlgorithm;
    }

    public ReferenceMatcher() {}

    private String matchAlgorithm = "Index";

    public static long matchTimes = 0;
    // We assume that `input` is the string representation of a json object.
    // { "c" : [{"id": 1, "ref": "foo"}, {"id": 2, "ref": "bar"}]}
    // We output the string representation of a json object.
    // { "c": [{"c": [{"id": 1, "ref": "foo"}]}]}
    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        List<ClusterObjectInterface> references = new LinkedList<>();
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        String setAlgorithm = inputObjects.optString("algo");
        if (setAlgorithm.isEmpty()) {
            setAlgorithm = matchAlgorithm;
        }
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            Reference reference = new Reference(obj.getString("id"), obj.getString("ref"));
            String modifiedReference = reference.getReference().toLowerCase().replaceAll(
                    "[^a-zА-я\\d ]", "").replaceAll(" ", "");
            reference.setModifiedReference(modifiedReference);
            Map<Character, Integer> counters = new TreeMap<>();
            for (int j = 0; j < modifiedReference.length(); j++) {
                Character c = modifiedReference.charAt(j);
                if (counters.containsKey(c)) {
                    counters.put(c, counters.get(c) + 1);
                } else {
                    counters.put(c, 1);
                }
            }
            reference.setCounters(counters);
            String counterStr ="";
            for (Character c : counters.keySet()) {
                counterStr += c + counters.get(c) + ",";
            }
            reference.setCountersHashValue(counterStr.hashCode());
            String translatedReference = modifiedReference;
            Map<String, String> translationMapping = new HashMap<>();
            translationMapping.put("8", "b");
            translationMapping.put("6", "g");
            translationMapping.put("i", "l");
            translationMapping.put("1", "l");
            translationMapping.put("0", "d");
            translationMapping.put("o", "d");
            translationMapping.put("q", "d");
            translationMapping.put("s", "5");
            translationMapping.put("z", "2");
            for (String key : translationMapping.keySet()) {
                translatedReference =  translatedReference.replaceAll(key, translationMapping.get(key));
            }
            reference.setTranslatedReference(translatedReference);
            references.add(reference);
        }

        if (setAlgorithm.equals("Graph")) {
            return GraphCluster.cluster(references);
        } else if (setAlgorithm.equals("Pairwise")) {
            return PairwiseCluster.cluster(references);
        } else if (setAlgorithm.equals("Index")) {
            ReferenceIndexer indexer = new ReferenceIndexer();
            indexer.index(references);
            IndexCluster indexCluster = new IndexCluster(indexer);
            return indexCluster.cluster(references);
        } else {
            throw new IllegalArgumentException("Unsupported match algorithm: " + setAlgorithm);
        }
    }

    // Computes the similarity between `left` and `right`.
    // Returns [0, 1] where 0 means not match, 1 means full match, and value in the middle means partial match.
    private static double computeSimilarity(Reference left, Reference right) {
        // Trie tree search.
        if (left.getModifiedReference().equals(right.getModifiedReference())) {
            return 1;
        }

        if (left.getModifiedReference().length() == 0 || right.getModifiedReference().length() == 0) {
            return 0;
        }

        // Trie tree search.
        if (left.getModifiedReference().length() > 4 && right.getModifiedReference().length() > 4) {
            StringPair strPair = getShortLongStr(left.getModifiedReference(), right.getModifiedReference());
            String longStr = strPair.getLongStr(), shortStr = strPair.getShortStr();
            if (longStr.startsWith(shortStr) || longStr.endsWith(shortStr)) {
                return 1;
            }
        }

        if (Math.abs(left.getModifiedReference().length() - right.getModifiedReference().length()) > 3) {
            return 0;
        }

        if (left.getModifiedReference().length() == right.getModifiedReference().length()) {
            // Trie tree search on translated reference.
            if (left.getTranslatedReference().equals(right.getTranslatedReference())) {
                return 1;
            }
            // Use counters hash as the index.
            if (left.getTranslatedReference().length() > 3 && left.getCounters().equals(right.getCounters())) {
                int numMismatches = 0;
                for (int i = 0; i < left.getModifiedReference().length(); i++) {
                    Character charLeft = left.getModifiedReference().charAt(i);
                    Character charRight = right.getModifiedReference().charAt(i);
                    if (charLeft.equals(charRight)) {
                        continue;
                    } else {
                        if (Character.isDigit(charLeft) || Character.isDigit(charRight)) {
                            return 0;
                        }
                        numMismatches++;
                        if (numMismatches >= 3) {
                            return 0;
                        }
                    }
                }
                return  1- numMismatches * 1.0 / 3.0;
            }
        } else {
            // Num error is capped at three. Thus, we could use trie tree to make it faster.
            // For searching longer str match from trie, just count the # of non-matches from the start.
            // Once exceeding the limit three, prune the whole sub-tree.
            // For searching shorter str match from trie, also count the # of skipped characters from the
            // start. Once exceeding the limit three, prune the sub-tree.
            StringPair strPair = getShortLongStr(left.getModifiedReference(), right.getModifiedReference());
            String longStr = strPair.getLongStr(), shortStr = strPair.getShortStr();
            int maxErrors = longStr.length() - shortStr.length();
            int numErrors = 0;
            int longIdx = 0;
            int numMatch = 0;
            for (int i = 0; i < shortStr.length(); i++) {
                if (longIdx == longStr.length() || numErrors > maxErrors) {
                    break;
                }
                if (shortStr.charAt(i) == longStr.charAt(longIdx)) {
                    numMatch++;
                    longIdx++;
                    continue;
                } else {
                    numErrors++;
                    longIdx++;
                    while (numErrors <= maxErrors && longIdx < longStr.length() &&
                            shortStr.charAt(i) != longStr.charAt(longIdx)) {
                        longIdx ++;
                        numErrors++;
                    }
                    if (numErrors > maxErrors || longIdx == longStr.length()) {
                        break;
                    } else {
                        numMatch ++;
                        longIdx ++;
                    }
                }
            }
            if (numMatch == shortStr.length()) {
                return 1;
            }
        }

        return 0;
    }

    private static StringPair getShortLongStr(String str1, String str2 ) {
        if (str1.length() < str2.length()) {
            return new StringPair(str1, str2);
        } else {
            return new StringPair(str2, str1);
        }
    }

    @RequiredArgsConstructor
    static class StringPair {
        @Getter
        private final String shortStr;
        @Getter
        private final String longStr;
    }

    @RequiredArgsConstructor
    @EqualsAndHashCode
    @ToString
    static class Reference implements ClusterObjectInterface {
        private static final double EPS = 1e-6;
        @Getter
        private final String rowId;
        @Getter
        private final String reference;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private String modifiedReference;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private Map<Character, Integer> counters;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private int countersHashValue;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private String translatedReference;

        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\", \"ref\": \"" + reference + "\"}";
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            matchTimes++;
            Reference otherReference = (Reference) other;
            if (computeSimilarity(this, otherReference) > EPS) {
                return true;
            }
            return false;
        }

        public String getId() {
            return rowId;
        }
    }
}
