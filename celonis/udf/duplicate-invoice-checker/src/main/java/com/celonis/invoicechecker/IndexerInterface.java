package com.celonis.invoicechecker;

import java.util.List;

public interface IndexerInterface {
    public void index(List<ClusterObjectInterface> clusterObjects);

    // Finds the objects connected to the object `id`.
    public List<String> findEdges(String id);
}
