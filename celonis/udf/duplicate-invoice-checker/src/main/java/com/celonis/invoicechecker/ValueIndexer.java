package com.celonis.invoicechecker;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

import java.util.*;

public class ValueIndexer implements IndexerInterface {
    public void index(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        Map<Integer, List<String>> counterToIds = new HashMap<>();
        List<Utils.ValueToId<Double>> valueToIds = new ArrayList<>();
        Map<String, Set<String>> potentialConnectedEdges = new HashMap<>();

        for (ClusterObjectInterface clusterObject : clusterObjects) {
            ValueMatcher.Value value = (ValueMatcher.Value) clusterObject;
            idToObjects.put(value.getId(), value);
            valueToIds.add(new Utils.ValueToId(value.getValue(), value.getId()));
            int hashCode = value.getCountersHashValue();
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
            List<String> counterMatches = counterToIds.get(value.getCountersHashValue());
            Utils.addEdges(id, counterMatches, potentialConnectedEdges);
            Utils.Range range = Utils.findRange(value.getValue()-80, value.getValue() + 80, valueToIds);
            List<String> linearDecayMatches = new ArrayList<>();
            for (int i = range.getLeft(); i < range.getRight(); i++) {
               linearDecayMatches.add(valueToIds.get(i).getId());
            }
            Utils.addEdges(id, linearDecayMatches, potentialConnectedEdges);
        }

        for (String id : idToObjects.keySet()) {
            List<String> connectedIds = new ArrayList<>();
            for (String pointedId : potentialConnectedEdges.get(id)) {
                if (pointedId != id && idToObjects.get(id).isSimilar(idToObjects.get(pointedId))) {
                    connectedIds.add(pointedId);
                }
            }
            connectedEdges.put(id, connectedIds);
        }
    }

    public List<String> findEdges(String id) {
        return connectedEdges.get(id);
    }

    private Map<String, List<String>> connectedEdges = new HashMap<>();
}
