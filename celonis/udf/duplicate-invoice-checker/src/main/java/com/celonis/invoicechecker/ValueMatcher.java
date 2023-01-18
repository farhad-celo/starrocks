package com.celonis.invoicechecker;

import org.json.*;
import java.util.List;
import java.util.LinkedList;
import java.util.ArrayList;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.EqualsAndHashCode;
import org.json.JSONObject;


public class ValueMatcher {
    // We assume that `input` is the string representation of a json object.
    // { "c" : [{"id": 1, "val": 0.2}, {"id": 2, "value": "}]}
    // We output the string representation of a json object.
    // { "c": [{"c": [{"id": 1, "ref": "foo"}]}]}
    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        List<ClusterObjectInterface> values = new LinkedList<>();
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            Value value = new Value(obj.getString("id"), obj.getDouble("val"));
            String normalizedValStr = Long.toString((long)(value.getValue() * 100));
            value.setNormalizedValueStr(normalizedValStr);
            values.add(value);
        }

        // return PairwiseCluster.cluster(values);
        return GraphCluster.cluster(values);
    }

    @RequiredArgsConstructor
    @EqualsAndHashCode
    @ToString
    static class Value implements ClusterObjectInterface {
        private static final double EPS = 1e-6;
        @Getter
        private final String rowId;

        @Getter
        private final double value;

        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private String normalizedValueStr;

        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\", \"val\": " + value + "}";
        }

        private double computeSimilarityScore(Value otherValue) {
            double absDiff = Math.abs(this.value - otherValue.value);
            if (absDiff < EPS) {
                return 1;
            }
            double linearDecaySimilarity = Math.max(0, 1-absDiff/80.0);
            double turnerSimilarity = 0;
            if (this.getNormalizedValueStr().length() != otherValue.getNormalizedValueStr().length()) {
                turnerSimilarity = 0;
            } else {
                int turners = 0;
                for (int i = 0; i < this.getNormalizedValueStr().length(); i++) {
                    if (this.getNormalizedValueStr().charAt(i) != otherValue.getNormalizedValueStr().charAt(i)) {
                        turners++;
                    }
                }
                turnerSimilarity = Math.max(0, 1-0.2 * turners);
            }

            return Math.max(linearDecaySimilarity, turnerSimilarity);
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            Value otherValue = (Value) other;
            if (computeSimilarityScore(otherValue) > EPS) {
                return true;
            }
            return false;
        }

        public String getId() {
            return rowId;
        }
    }
}
