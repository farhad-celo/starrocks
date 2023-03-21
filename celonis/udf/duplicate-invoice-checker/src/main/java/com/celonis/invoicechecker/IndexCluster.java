package com.celonis.invoicechecker;

import lombok.RequiredArgsConstructor;

import java.util.*;

@RequiredArgsConstructor
public class IndexCluster {
    public String[] cluster(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        for (ClusterObjectInterface clusterObjectInterface : clusterObjects) {
            idToObjects.put(clusterObjectInterface.getId(), clusterObjectInterface);
        }
        List<String> sortedIds = new ArrayList<String>(idToObjects.keySet());
        Collections.sort(sortedIds);

        Set<String> clusteredNodes = new HashSet<>();
        List<Cluster<ClusterObjectInterface>> clusters = new ArrayList<>();
        for (String id : sortedIds) {
            if (clusteredNodes.contains(id)) {
                continue;
            }
            Set<String> candidateIds = new HashSet<>();
            candidateIds.add(id);
            Cluster<ClusterObjectInterface> cluster = new Cluster<ClusterObjectInterface>();
            while (!candidateIds.isEmpty()) {
                Set<String> nextLevelCandidateIds = new HashSet<>();
                for (String candidateId : candidateIds) {
                    if (clusteredNodes.contains(candidateId)) {
                        continue;
                    }
                    clusteredNodes.add(candidateId);
                    cluster.getClusterObjects().add(idToObjects.get(candidateId));
                    Set<String> adjacentNodes = indexer.findEdges(candidateId);
                    if (adjacentNodes != null) {
                        for (String nextLevelCandidateId : adjacentNodes) {
                            if (!clusteredNodes.contains(nextLevelCandidateId)) {
                                nextLevelCandidateIds.add(nextLevelCandidateId);
                            }
                        }
                    }
                }
                candidateIds = nextLevelCandidateIds;
            }
            if (cluster.getClusterObjects().size() > 1) {
                clusters.add(cluster);
            }
        }
        String[] clusterStrs = new String[clusters.size()];
        for (int i = 0; i < clusters.size(); i++) {
            clusterStrs[i] = clusters.get(i).toJsonString();
        }
        return clusterStrs;
    }

    private final IndexerInterface indexer;
}
