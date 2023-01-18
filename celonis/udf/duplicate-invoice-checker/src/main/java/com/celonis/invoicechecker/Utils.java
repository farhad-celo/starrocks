package com.celonis.invoicechecker;

public class Utils {
    static String wrapArrayObject(String objectName, String objectStr) {
        return "{ \"" + objectName + "\": [" + objectStr + "]}";
    }
}
