package com.celonis.invoicechecker;

import lombok.EqualsAndHashCode;
import org.json.*;
import lombok.RequiredArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;

import java.sql.Array;
import java.util.Iterator;
import java.util.List;
import java.util.ArrayList;
import java.util.LinkedList;



public class CompanyMatcher {
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
            String modifiedCompanyName = company.getVendorName().toLowerCase().replaceAll("[^a-zА-я\\d ]",
                    "");
            modifiedCompanyName = modifiedCompanyName.replaceAll(" +", " ").trim();
            modifiedCompanyName = modifiedCompanyName.replaceAll(" gmbh| ag | llc| inc| ltd| limited|" +
                    " sdn| bhd| se| corporation| corp| sl| coltd| group| mbh| co| kg| ltda| sa| sro| des| sas| sasu|"
                    + "zoo| sp| sau| cokg", "");
            company.setModifiedVendorName(modifiedCompanyName.replaceAll(" ", ""));
            companies.add(company);
        }


        //return PairwiseCluster.cluster(companies);
        return GraphCluster.cluster(companies);
    }

    @RequiredArgsConstructor
    @EqualsAndHashCode
    @ToString
    static class Company implements ClusterObjectInterface {
        @Getter
        private final String rowId;
        @Getter
        private final String vendorName;
        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private String modifiedVendorName;

        private final static UDFJaroDistance distance = new UDFJaroDistance();

        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\", \"vendor_name\": \"" + vendorName + "\"}";
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            Company otherCompany = (Company) other;
            // TODO(f.li): Upgrade commons-text library to version 1.10.0 and use the new threshold. We
            // currently use version 1.4.0 to match the one used in SR's runtime. We should figure it out why SR's
            // runtime can only use version 1.4.0.
            if (distance.evaluate(this.getModifiedVendorName(), otherCompany.getModifiedVendorName()) > 0.85) {
                return true;
            }
            return false;
        }

        public String getId() {
            return rowId;
        }
    }
}
