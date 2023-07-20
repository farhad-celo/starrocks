package com.celonis.invoicechecker;

import org.json.JSONObject;

import java.util.*;

public class ValueIndexer implements IndexerInterface {
    private static final int priority = 0;
    private double maxPriceLimit = 80.0;
    private final Map<String, Set<String>> connectedEdges = new HashMap<>();
    public String columnName = "";

    public ValueIndexer() {
    }

    public ValueIndexer(String columnName) {
        this.columnName = columnName;
    }

    public ValueIndexer(String columnName, JSONObject params) {
        this.columnName = columnName;
        if (params == null) return;
        if (params.has("max_price_limit")) {
            maxPriceLimit = params.getDouble("max_price_limit");
        }
    }

    public void index(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        Map<Integer, List<String>> counterToIds = new HashMap<>();
        List<Utils.ValueToId<Double>> valueToIds = new ArrayList<>();
        Map<String, Set<String>> potentialConnectedEdges = new HashMap<>();

        for (ClusterObjectInterface clusterObject : clusterObjects) {
            ValueMatcher.Value value = (ValueMatcher.Value) clusterObject;
            idToObjects.put(value.getId(), value);
            valueToIds.add(new Utils.ValueToId(value.getValue(), value.getId()));
            int hashCode = value.getCounterHashValue();
            if (counterToIds.containsKey(hashCode)) {
                counterToIds.get(hashCode).add(value.getId());
            } else {
                List<String> ids = new ArrayList<>();
                ids.add(value.getId());
                counterToIds.put(hashCode, ids);
            }
        }

        Collections.sort(valueToIds, (left, right) -> left.getValue().compareTo(right.getValue()));

        for (String id : idToObjects.keySet()) {
            ValueMatcher.Value value = (ValueMatcher.Value) idToObjects.get(id);
            List<String> counterMatches = counterToIds.get(value.getCounterHashValue());
            Utils.addEdges(id, counterMatches, potentialConnectedEdges);
            Utils.Range range = Utils.findRange(value.getValue()-80, value.getValue() + 80, valueToIds);
            List<String> linearDecayMatches = new ArrayList<>();
            for (int i = range.getLeft(); i < range.getRight(); i++) {
               linearDecayMatches.add(valueToIds.get(i).getId());
            }
            Utils.addEdges(id, linearDecayMatches, potentialConnectedEdges);
        }

        for (String id : idToObjects.keySet()) {
            Set<String> connectedIds = new HashSet<>();
            for (String pointedId : potentialConnectedEdges.get(id)) {
                if (pointedId != id && idToObjects.get(id).isSimilar(idToObjects.get(pointedId))) {
                    connectedIds.add(pointedId);
                }
            }
            connectedEdges.put(id, connectedIds);
        }
    }

    public Set<String> findEdges(String id, Set<String> candidateIds) {
        Set<String> resSet = connectedEdges.get(id);
        if (candidateIds == null) return new HashSet<>(resSet);
        candidateIds.retainAll(resSet);
        return candidateIds;
    }

    public ClusterObjectInterface createClusterObjFromJson(JSONObject obj) {
        String rowId = obj.getString("id");
        Double content = obj.getDouble(columnName);
        ValueMatcher.Value val = new ValueMatcher.Value(rowId, content);
        val.setMaxPriceLimit(maxPriceLimit);
        return ValueMatcher.preprocess(val);
    }

    public Integer getPriority() {
        return priority;
    }
}
