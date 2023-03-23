package com.celonis.invoicechecker;

import org.json.JSONObject;

import java.util.*;

public class ReferenceIndexer implements IndexerInterface {
    private static final int priority = 2;
    private final Map<String, Set<String>> connectedEdges = new HashMap<>();

    private int maxErrors = 3;
    public String columnName = "";

    public ReferenceIndexer() {
    }

    public ReferenceIndexer(String columnName) {
        this.columnName = columnName;
    }

    public ReferenceIndexer(String columnName, JSONObject params) {
        this.columnName = columnName;
        if (params == null) return;
        if (params.has("max_errors")) {
            maxErrors = params.getInt("max_errors");
        }
    }

    public void index(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        Map<String, Set<String>> potentialConnectedEdges = new HashMap<>();
        TrieTree modifiedStrTrie = new TrieTree();
        TrieTree reversedModifiedStrTrie = new TrieTree();
        TrieTree translatedStrTrie = new TrieTree();
        Map<Integer, List<String>> counterToIds = new HashMap<>();
        for (ClusterObjectInterface clusterObjectInterface : clusterObjects) {
            idToObjects.put(clusterObjectInterface.getId(), clusterObjectInterface);
            ReferenceMatcher.Reference reference = (ReferenceMatcher.Reference) clusterObjectInterface;
            modifiedStrTrie.add(reference.getModifiedReference(), reference.getId());
            reversedModifiedStrTrie.add(new StringBuilder(reference.getModifiedReference()).reverse().toString(),
                    reference.getId());
            translatedStrTrie.add(reference.getTranslatedReference(), reference.getId());
            int hashCode = reference.getCountersHashValue();
            if (counterToIds.containsKey(hashCode)) {
                counterToIds.get(hashCode).add(reference.getId());
            } else {
                List<String> ids = new ArrayList<>();
                ids.add(reference.getId());
                counterToIds.put(hashCode, ids);
            }
        }
        for (String id : idToObjects.keySet()) {
            ReferenceMatcher.Reference ref = (ReferenceMatcher.Reference) idToObjects.get(id);
            List<String> modifiedMatches = modifiedStrTrie.findPrefix(ref.getModifiedReference());
            List<String> reversedModifiedMatches = reversedModifiedStrTrie.findPrefix(
                    new StringBuilder(ref.getModifiedReference()).reverse().toString());
            List<String> translatedMatches = translatedStrTrie.findPrefixWithFuzzyMatch(ref.getTranslatedReference(), maxErrors);
            List<String> counterMatches = counterToIds.get(ref.getCountersHashValue());
            Utils.addEdges(id, modifiedMatches, potentialConnectedEdges);
            Utils.addEdges(id, reversedModifiedMatches, potentialConnectedEdges);
            Utils.addEdges(id, translatedMatches, potentialConnectedEdges);
            Utils.addEdges(id, counterMatches, potentialConnectedEdges);
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
        String content = obj.getString(columnName);
        ReferenceMatcher.Reference ref = new ReferenceMatcher.Reference(rowId, content);
        ref.setMaxErrors(maxErrors);
        return ReferenceMatcher.preprocess(ref);
    }

    public Integer getPriority() {
        return priority;
    }
}
