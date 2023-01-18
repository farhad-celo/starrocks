package com.celonis.invoicechecker;

public interface ClusterObjectInterface {
    public String toJsonString();

    public boolean isSimilar(ClusterObjectInterface other);

    public String getId();
}
