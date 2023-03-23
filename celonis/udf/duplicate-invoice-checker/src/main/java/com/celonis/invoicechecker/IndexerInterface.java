package com.celonis.invoicechecker;

import org.json.JSONObject;

import java.util.List;
import java.util.Set;

public interface IndexerInterface {
    void index(List<ClusterObjectInterface> clusterObjects);

    // Finds the objects connected to the object `id`.
    default Set<String> findEdges(String id) {
        return this.findEdges(id, null);
    }

    // Find the objects connected to the object `id`.
    // If candidiateIds is null, the indexer will consider all other indexes as candidates.
    // If candidatesIds is not null, the indexer will only find connected objects within candidates.
    // TO-DO: Maybe put the intersection on the caller side.
    Set<String> findEdges(String targetId, Set<String> candidateIds);

    ClusterObjectInterface createClusterObjFromJson(JSONObject obj);

    Integer getPriority();
}
