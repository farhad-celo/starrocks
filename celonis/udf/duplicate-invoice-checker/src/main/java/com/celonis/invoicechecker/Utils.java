package com.celonis.invoicechecker;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

import java.util.*;

public class Utils {

    static String dataListToJsonStr(List<List<String>> dataset) {
        List<String> headers = dataset.get(0);
        String prefix = "\"c\": [";
        String suffix = "]";
        List<String> rows = new ArrayList<>();
        for (int i = 1; i < dataset.size(); i++) {
            List<String> row = new ArrayList<>();
            for (int j = 0; j < headers.size(); j++) {
                String column = addQuotes(headers.get(j)) + ": " + addQuotes(dataset.get(i).get(j));
                row.add((column));
            }
            rows.add("{ " + String.join(", ", row) + " }");
        }
        return prefix + String.join(", ", rows) + suffix;
    }

    static String patternListToJsonStr(List<List<String>> patterns) {
        String prefix = "\"p\": [";
        String suffix = "]";
        List<String> rows = new ArrayList<>();
        for (List<String> pattern : patterns) {
            String columnName = addQuotes("columnName") + ": " + addQuotes(pattern.get(0));
            String comparer = addQuotes("comparer") + ": {" + addQuotes("comparerName") + ": " + addQuotes(pattern.get(1));
            if (pattern.get(2) == "") comparer += "}";
            else comparer += ", \"parameters\": " + pattern.get(2) + "}";
            rows.add("{ " + columnName + ", " + comparer + " }");
        }
        return prefix + String.join(", ", rows) + suffix;
    }

    static String concatDataPattern(String data, String pattern) {
        return "{ " + data + ", " + pattern + " }";
    }

    static String addQuotes(String c) {
        return "\"" + c + "\"";
    }

    static String wrapArrayObject(String objectName, String objectStr) {
        return "{ \"" + objectName + "\": [" + objectStr + "]}";
    }

    static Map<Character, Integer> getCharacterCounter(String str) {
        Map<Character, Integer> counter = new HashMap<>();
        for (int j = 0; j < str.length(); j++) {
            Character c = str.charAt(j);
            counter.put(c, counter.getOrDefault(c, 0) + 1);
        }

        return counter;
    }

    static void addEdges(String id, Collection<String> ids, Map<String, Set<String>> potentialConnectedEdges) {
        potentialConnectedEdges.computeIfAbsent(id, k -> new HashSet<>()).addAll(ids);
        for (String pointedId : ids) {
            potentialConnectedEdges.computeIfAbsent(pointedId, k -> new HashSet<>()).add(id);
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
