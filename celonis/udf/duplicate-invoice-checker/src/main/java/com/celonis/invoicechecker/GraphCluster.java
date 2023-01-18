package com.celonis.invoicechecker;
import java.util.*;


public class GraphCluster {
    public static String[] cluster(List<ClusterObjectInterface> clusterObjects) {
        Map<String, ClusterObjectInterface> idToObjects = new HashMap<>();
        Map<String, List<String>> connectedEdges = new HashMap<>();
        for (ClusterObjectInterface clusterObjectInterface : clusterObjects) {
            idToObjects.put(clusterObjectInterface.getId(), clusterObjectInterface);
        }
        List<String> sortedIds = new ArrayList<String>(idToObjects.keySet());
        Collections.sort(sortedIds);
        for (int i = 0; i < sortedIds.size(); i++) {
            for (int j = i + 1; j < sortedIds.size(); j++) {
                String leftKey = sortedIds.get(i);
                String rightKey = sortedIds.get(j);
                ClusterObjectInterface leftObject = idToObjects.get(leftKey);
                ClusterObjectInterface rightObject = idToObjects.get(rightKey);
                if (leftObject.isSimilar(rightObject)) {
                    if (connectedEdges.containsKey(leftKey)) {
                        connectedEdges.get(leftKey).add(rightKey);
                    } else {
                        List<String> ids = new ArrayList<>();
                        ids.add(rightKey);
                        connectedEdges.put(leftKey, ids);
                    }
                    if (connectedEdges.containsKey(rightKey)) {
                        connectedEdges.get(rightKey).add(leftKey);
                    } else {
                        List<String> ids = new ArrayList<>();
                        ids.add(leftKey);
                        connectedEdges.put(rightKey, ids);
                    }
                }
            }
        }

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
                    List<String> adjacentNodes = connectedEdges.get(candidateId);
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
}
