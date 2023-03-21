package com.celonis.invoicechecker;

import java.util.*;

public class ReferenceIndexer implements IndexerInterface {
    private final Map<String, Set<String>> connectedEdges = new HashMap<>();

    private int maxErrors = 3;

    public ReferenceIndexer() {
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

    public Set<String> findEdges(String id) {
        return connectedEdges.get(id);
    }
}
