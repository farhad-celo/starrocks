package com.celonis.invoicechecker;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

import java.util.ArrayList;
import java.util.List;

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
