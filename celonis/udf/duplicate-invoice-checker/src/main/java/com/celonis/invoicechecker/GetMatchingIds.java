package com.celonis.invoicechecker;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class GetMatchingIds {
    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        List<String> ids = new ArrayList<>();
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            ids.add(obj.getString("id"));
        }
        String[] retStrs = new String[ids.size()];
        for (int i = 0; i < ids.size(); i++) {
            retStrs[i] = ids.get(i);
        }
        return retStrs;
    }
}
