package com.celonis.invoicechecker;
import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;

public class PairwiseCluster {
    public static String[] cluster(List<ClusterObjectInterface> clusterObjects) {
        List<Cluster<ClusterObjectInterface>> clusters = new ArrayList<>();
        while (!clusterObjects.isEmpty()) {
            Cluster<ClusterObjectInterface> cluster = new Cluster<ClusterObjectInterface>();
            cluster.getClusterObjects().add(clusterObjects.get(0));
            clusterObjects.remove(0);
            while(true) {
                boolean expanded = false;
                for (Iterator<ClusterObjectInterface> iter = clusterObjects.iterator(); iter.hasNext(); ) {
                    ClusterObjectInterface candidateClusterObject = iter.next();
                    for (ClusterObjectInterface clusterObject : cluster.getClusterObjects()) {
                        if (clusterObject.isSimilar(candidateClusterObject)) {
                            cluster.getClusterObjects().add(candidateClusterObject);
                            expanded = true;
                            iter.remove();
                            break;
                        }
                    }
                    if (expanded) break;
                }
                if (!expanded) break;
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
