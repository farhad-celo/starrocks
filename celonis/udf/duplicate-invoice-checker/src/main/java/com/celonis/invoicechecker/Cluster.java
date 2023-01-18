package com.celonis.invoicechecker;

import lombok.RequiredArgsConstructor;
import lombok.Getter;
import java.util.List;
import java.util.ArrayList;

@RequiredArgsConstructor
public class Cluster<T extends ClusterObjectInterface> {

    @Getter
    private final List<T> clusterObjects = new ArrayList<>();

    public String toJsonString() {
        List<String> objectStrings = new ArrayList<>();
        for (T clusterObject : clusterObjects) {
            objectStrings.add(clusterObject.toJsonString());
        }

        return Utils.wrapArrayObject("c", String.join(", ", objectStrings));
    }
}
