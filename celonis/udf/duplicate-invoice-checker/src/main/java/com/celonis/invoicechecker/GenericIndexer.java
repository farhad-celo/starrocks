package com.celonis.invoicechecker;

import lombok.RequiredArgsConstructor;

import java.util.*;

@RequiredArgsConstructor
public class GenericIndexer {

    private final List<IndexerInterface> indexers;
    private final Map<String, Set<String>> connectedEdges = new HashMap<>();

    public void index(List<GenericMatcher.Invoice> invoices) {
        if (indexers.size() == 0)
            throw new IllegalArgumentException("Indexers are empty");
        for (int i = 0; i < indexers.size(); i++) {
            IndexerInterface indexer = indexers.get(i);
            List<ClusterObjectInterface> rows = new LinkedList<>();

            for (GenericMatcher.Invoice invoice : invoices) {
                rows.add(invoice.getInvoiceAttrs().get(i));
            }
            if (indexer != null) indexer.index(rows);
        }
        Set<String> idSet = new HashSet<>();
        for (GenericMatcher.Invoice invoice : invoices) {
            idSet.add(invoice.getId());
        }
        for (String id : idSet) {
            Set<String> candidates = indexers.get(0).findEdges(id);
            for (int i = 1; i < indexers.size(); i++) {
                if (candidates.size() == 0) break;
                candidates = indexers.get(i).findEdges(id, candidates);
            }
            if (candidates.size() > 0) {
                connectedEdges.put(id, candidates);
            }
        }
    }

    public Set<String> findEdges(String id) {
        return connectedEdges.get(id);
    }

    public Set<String> getIds() {
        return connectedEdges.keySet();
    }
}
