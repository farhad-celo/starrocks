package com.celonis.invoicechecker;

import org.apache.commons.text.similarity.JaroWinklerDistance;

public class UDFJaroWinklerDistance {
    private final JaroWinklerDistance distance = new JaroWinklerDistance();
    public final Double evaluate(String left, String right) {
        return distance.apply(left, right);
    }
}
