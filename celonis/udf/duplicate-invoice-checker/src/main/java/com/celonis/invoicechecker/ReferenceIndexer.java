package com.celonis.invoicechecker;
import java.util.*;

public class ReferenceIndexer implements IndexerInterface {
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
            List<String> translatedMatches = translatedStrTrie.findPrefixWithFuzzyMatch(ref.getTranslatedReference(), 3);
            List<String> counterMatches = counterToIds.get(ref.getCountersHashValue());
            addEdges(id, modifiedMatches, potentialConnectedEdges);
            addEdges(id, reversedModifiedMatches, potentialConnectedEdges);
            addEdges(id, translatedMatches, potentialConnectedEdges);
            addEdges(id, counterMatches, potentialConnectedEdges);
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

    private void addEdges(String id, List<String> ids, Map<String, Set<String>> potentialConnectedEdges) {
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

    private Map<String, List<String>> connectedEdges = new HashMap<>();
}
