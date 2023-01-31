package com.celonis.invoicechecker;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

import java.util.*;

public class Utils {
    static String wrapArrayObject(String objectName, String objectStr) {
        return "{ \"" + objectName + "\": [" + objectStr + "]}";
    }

    static Map<Character, Integer> getCharacterCounts(String str) {
        Map<Character, Integer> counters = new TreeMap<>();
        for (int j = 0; j < str.length(); j++) {
            Character c = str.charAt(j);
            if (counters.containsKey(c)) {
                counters.put(c, counters.get(c) + 1);
            } else {
                counters.put(c, 1);
            }
        }

        return counters;
    }

    static void addEdges(String id, Collection<String> ids, Map<String, Set<String>> potentialConnectedEdges) {
        if (potentialConnectedEdges.containsKey(id)) {
            potentialConnectedEdges.get(id).addAll(ids);
        } else {
            Set<String> hashIds = new HashSet<>(ids);
            potentialConnectedEdges.put(id, hashIds);
        }
        for (String pointedId : ids) {
            if (potentialConnectedEdges.containsKey(pointedId)) {
                potentialConnectedEdges.get(pointedId).add(id);
            } else {
                Set<String> hashIds = new HashSet<>();
                hashIds.add(id);
                potentialConnectedEdges.put(pointedId, hashIds);
            }
        }
    }

    public static<T extends Comparable<T>> Range findRange(T leftValue, T rightValue, List<ValueToId<T>> valueToIds) {
        Range range = new Range();
        // Search for the left end.
        int left = 0, right = valueToIds.size();
        while (right - left > 1) {
            int mid = (left + right) / 2;
            if (valueToIds.get(mid).getValue().compareTo(leftValue) > 0) {
                right = mid;
            } else {
                left = mid;
            }
        }
        range.setLeft(left);

        // Search for the right end.
        left = 0;
        right = valueToIds.size();
        while (right - left > 1) {
            int mid = (left + right) / 2;
            if (valueToIds.get(mid).getValue().compareTo(rightValue) > 0) {
               right = mid;
            } else {
                left = mid;
            }
        }
        range.setRight(right);

        return range;
    }

    @AllArgsConstructor
    public final static class ValueToId<T extends Comparable<T>> {
        @Setter
        @Getter
        private T value;
        @Setter
        @Getter
        private String id;
    }

    @AllArgsConstructor
    @NoArgsConstructor
    public final static class Range {
        @Setter
        @Getter
        private int left;
        @Setter
        @Getter
        private int right;
    }
}
