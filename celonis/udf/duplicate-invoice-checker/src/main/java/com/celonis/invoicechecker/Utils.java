package com.celonis.invoicechecker;

import java.util.Map;
import java.util.TreeMap;

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
}
