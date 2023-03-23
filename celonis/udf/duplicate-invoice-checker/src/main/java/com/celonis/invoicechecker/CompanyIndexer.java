package com.celonis.invoicechecker;

import org.json.JSONObject;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class CompanyIndexer implements IndexerInterface {

    private static final int priority = 3;

    private double threshold = 0.85;
    private final Map<String, CompanyMatcher.Company> idToObject = new HashMap<>();
    public String columnName = "";

    public CompanyIndexer() {
    }

    public CompanyIndexer(String columnName) {
        this.columnName = columnName;
    }

    public CompanyIndexer(String columnName, JSONObject params) {
        this.columnName = columnName;
        if (params == null) return;
        if (params.has("threshold")) {
            threshold = params.getDouble("threshold");
        }
    }

    public void index(List<ClusterObjectInterface> clusterObjects) {
        for (ClusterObjectInterface clusterObject : clusterObjects) {
            CompanyMatcher.Company company = (CompanyMatcher.Company) clusterObject;
            idToObject.put(company.getId(), company);
        }
    }

    public Set<String> findEdges(String targetId, Set<String> candidateIds) {
        if (candidateIds == null) {
            candidateIds = idToObject.keySet();
        }
        candidateIds.remove(targetId);
        CompanyMatcher.Company target = idToObject.get(targetId);
        candidateIds.removeIf(candidate -> !target.isSimilar(idToObject.get(candidate)));
        return candidateIds;
    }

    public ClusterObjectInterface createClusterObjFromJson(JSONObject obj) {
        String rowId = obj.getString("id");
        String content = obj.getString(columnName);
        CompanyMatcher.Company company = new CompanyMatcher.Company(rowId, content);
        company.setThreshold(threshold);
        return CompanyMatcher.preprocess(company);
    }

    public Integer getPriority() {
        return priority;
    }
}
