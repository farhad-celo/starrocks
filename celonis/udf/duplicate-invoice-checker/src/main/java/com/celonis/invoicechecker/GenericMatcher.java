package com.celonis.invoicechecker;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.*;

public class GenericMatcher {
    private List<IndexerInterface> indexers;
    private List<Invoice> invoices;

    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        JSONArray columnArray = inputObjects.getJSONArray("p");
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        indexers = new ArrayList<>();
        invoices = new ArrayList<>();
        for (int i = 0; i < columnArray.length(); i++) {
            JSONObject column = columnArray.getJSONObject(i);
            indexers.add(initiateIndexer(column));
        }
        indexers.sort(Comparator.comparing(IndexerInterface::getPriority));
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            List<ClusterObjectInterface> invoiceAttrs = createInvoiceAttrsFromJson(obj);
            if (invoiceAttrs != null) {
                invoices.add(new Invoice(obj.getString("id"), invoiceAttrs));
            }
        }
        if (invoices.size() <= 1) return new String[0];

        if (invoices.size() == 2) {
            Invoice invoice1 = invoices.get(0);
            Invoice invoice2 = invoices.get(1);
            if (!invoice1.isSimilar(invoice2)) {
                return new String[0];
            }
            return new String[]{invoice1.getId() + ", " + invoice2.getId()};
        }
        GenericIndexer genericIndexer = new GenericIndexer(indexers);
        genericIndexer.index(invoices);
        return cluster(genericIndexer);
    }

    // TO-DO: Maybe reuse the index cluster or refactor the code.
    private String[] cluster(GenericIndexer genericIndexer) {
        Set<String> visited = new HashSet<>();
        List<List<String>> clusters = new ArrayList<>();
        List<String> ids = new ArrayList<>(genericIndexer.getIds());
        Collections.sort(ids);
        for (String id : ids) {
            if (visited.contains(id)) continue;
            Queue<String> queue = new ArrayDeque<>();
            List<String> cluster = new ArrayList<>();
            queue.add(id);
            while (!queue.isEmpty()) {
                String curr = queue.poll();
                if (visited.contains(curr)) continue;
                cluster.add(curr);
                visited.add(curr);
                queue.addAll(genericIndexer.findEdges(curr));
            }
            clusters.add(cluster);
        }
        String[] clusterStrs = new String[clusters.size()];
        for (int i = 0; i < clusters.size(); i++) {
            clusterStrs[i] = String.join(", ", clusters.get(i));
        }
        return clusterStrs;
    }

    private IndexerInterface initiateIndexer(JSONObject column) {
        String columnName = column.getString("columnName");
        JSONObject comparer = column.getJSONObject("comparer");
        String comparerName = comparer.getString("comparerName");
        JSONObject params = null;
        if (comparer.has("parameters")) {
            params = comparer.getJSONObject("parameters");
        }
        switch (comparerName) {
            case "CompanyNameFuzzy":
                return new CompanyIndexer(columnName, params);
            case "DateFuzzy":
                return new DateTimeIndexer(columnName, params);
            case "InvoiceReferenceFuzzy":
                return new ReferenceIndexer(columnName, params);
            case "InvoiceValueFuzzy":
                return new ValueIndexer(columnName, params);
            default:
                throw new IllegalArgumentException("comparer name is not valid");
        }
    }


    private List<ClusterObjectInterface> createInvoiceAttrsFromJson(JSONObject obj) {
        List<ClusterObjectInterface> invoiceAttrs = new ArrayList<>();
        for (IndexerInterface indexer : indexers) {
            ClusterObjectInterface clusterObj = indexer.createClusterObjFromJson(obj);
            if (clusterObj == null) return null;
            invoiceAttrs.add(clusterObj);
        }
        return invoiceAttrs;
    }

    @RequiredArgsConstructor
    static class Invoice implements ClusterObjectInterface {
        @Getter
        private final String rowId;
        @Getter
        private final List<ClusterObjectInterface> invoiceAttrs;


        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\"}";
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            Invoice otherInvoice = (Invoice) other;
            List<ClusterObjectInterface> otherAttrs = otherInvoice.getInvoiceAttrs();
            for (int i = 0; i < otherAttrs.size(); i++) {
                if (!this.invoiceAttrs.get(i).isSimilar(otherAttrs.get(i))) {
                    return false;
                }
            }
            return true;
        }

        public String getId() {
            return rowId;
        }
    }
}
