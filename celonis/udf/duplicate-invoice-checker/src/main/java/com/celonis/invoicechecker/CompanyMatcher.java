package com.celonis.invoicechecker;

import lombok.*;
import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;


public class CompanyMatcher {
    public static Company preprocess(Company company) {
        String modifiedCompanyName = company.getVendorName().toLowerCase().replaceAll("[^a-zА-я\\d ]",
                "");
        modifiedCompanyName = modifiedCompanyName.replaceAll(" +", " ").trim();
        Set<String> suffix = Stream.of("gmbh", "ag", "llc", "inc", "ltd", "limited", "sdn", "bhd", "se",
                "corporation", "corp", "sl", "coltd", "group", "mbh", "co", "kg", "ltda", "sa", "sro",
                "des", "sas", "sasu", "zoo", "sp", "sau", "cokg"
        ).collect(Collectors.toCollection(HashSet::new));
        String[] words = modifiedCompanyName.split(" ");
        for (int i = words.length - 1; i >= 0; i--) {
            if (suffix.contains(words[i])) {
                words[i] = "";
            } else {
                break;
            }
        }
        modifiedCompanyName = String.join("", words);
        company.setModifiedVendorName(modifiedCompanyName);
        return company;
    }

    // We assume that `input` is the string representation of a json object.
    // { "companies" : [{"id": 1, "vendor_name": "foo"}, {"id" : 2, "vendor_name": "bar"}]}
    // We output the string representation of a json object.
    // { "clusters": [{"companies": [{"id": 1, "vendor_name": "foo"}]}]}
    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        List<ClusterObjectInterface> companies = new LinkedList<>();
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            Company company = new Company(obj.getString("id"), obj.getString("vendor_name"));
            companies.add(preprocess(company));
        }


        return GraphCluster.cluster(companies);
    }


    @RequiredArgsConstructor
    @EqualsAndHashCode
    @ToString
    static class Company implements ClusterObjectInterface {
        private final static UDFJaroDistance distance = new UDFJaroDistance();
        @Getter
        private final String rowId;
        @Getter
        private final String vendorName;
        @Setter
        private double threshold = 0.85;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private String modifiedVendorName;

        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\", \"vendor_name\": \"" + vendorName + "\"}";
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            Company otherCompany = (Company) other;
            // TODO(f.li): Upgrade commons-text library to version 1.10.0 and use the new threshold. We
            // currently use version 1.4.0 to match the one used in SR's runtime. We should figure it out why SR's
            // runtime can only use version 1.4.0.
            return (1.0 - distance.evaluate(this.getModifiedVendorName(), otherCompany.getModifiedVendorName())) > threshold;
        }

        public String getId() {
            return rowId;
        }
    }
}
