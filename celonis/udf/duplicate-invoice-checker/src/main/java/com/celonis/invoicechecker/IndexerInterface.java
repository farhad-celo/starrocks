package com.celonis.invoicechecker;

import java.util.List;
import java.util.Set;

public interface IndexerInterface {
    void index(List<ClusterObjectInterface> clusterObjects);

    // Finds the objects connected to the object `id`.
    Set<String> findEdges(String id);

}
