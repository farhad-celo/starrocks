package com.celonis.invoicechecker;

import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;

class GenericMatcherTest {
    private final List<List<String>> datasetList = Arrays.asList(
            Arrays.asList("id", "ref", "vendor", "date", "val"),
            Arrays.asList("1", "Booking123", "Celonis SE", "2020-09-01 01:00:00", "1000"),
            Arrays.asList("2", "Booking123", "Celonis SE", "2020-09-01 01:00:00", "1000"),
            Arrays.asList("3", "asdas", "Celonis SE", "2020-08-01 01:00:00", "1000"),
            Arrays.asList("4", "1234", "unipath", "2020-10-01 01:00:00", "220"),
            Arrays.asList("5", "888666", "Unipath Corp.", "2020-10-01 01:00:00", "220"),
            Arrays.asList("6", "AR-1234", "Unicorn Corp.", "2020-10-01 01:00:00", "220"),
            Arrays.asList("7", "AR-1234", "Uipath Corp.", "2020-01-10 01:00:00", "220"),
            Arrays.asList("8", "AR-1234", "Uipath Corp.", "2020-01-10 01:00:00", "230"),
            Arrays.asList("9", "A4321", "Nike", "2020-12-20 01:00:00", "100"),
            Arrays.asList("10", "4321", "Nike LLC", "2020-12-23 01:00:00", "101"),
            Arrays.asList("11", "4321", "Adidas LLC", "2020-12-23 01:00:00", "101")
    );
    private final String datasetStr = Utils.dataListToJsonStr(datasetList);

    private final List<List<String>> oneFuzzy = Collections.singletonList(
            Arrays.asList("date", "DateFuzzy", ""));

    private final List<List<String>> twoFuzzy = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", ""));

    private final List<List<String>> threeFuzzy = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", ""),
            Arrays.asList("vendor", "CompanyNameFuzzy", ""));

    private final List<List<String>> allFuzzy = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", ""),
            Arrays.asList("vendor", "CompanyNameFuzzy", ""),
            Arrays.asList("ref", "InvoiceReferenceFuzzy", ""));

    private final List<List<String>> dateParams = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", "{\"num_days\": \"1\"}"),
            Arrays.asList("val", "InvoiceValueFuzzy", ""),
            Arrays.asList("vendor", "CompanyNameFuzzy", ""),
            Arrays.asList("ref", "InvoiceReferenceFuzzy", ""));

    private final List<List<String>> valParams = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", "{\"max_price_limit\": \"5\"}"),
            Arrays.asList("vendor", "CompanyNameFuzzy", ""),
            Arrays.asList("ref", "InvoiceReferenceFuzzy", ""));

    private final List<List<String>> vendorParams = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", ""),
            Arrays.asList("vendor", "CompanyNameFuzzy", "{\"threshold\": \"0\"}"),
            Arrays.asList("ref", "InvoiceReferenceFuzzy", ""));

    private final List<List<String>> refParams = Arrays.asList(
            Arrays.asList("date", "DateFuzzy", ""),
            Arrays.asList("val", "InvoiceValueFuzzy", ""),
            Arrays.asList("vendor", "CompanyNameFuzzy", ""),
            Arrays.asList("ref", "InvoiceReferenceFuzzy", "{\"max_errors\": \"1\"}"));


    @Test
    void testOneFuzzy() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(oneFuzzy));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2, 4, 5, 6, 7, 8", "10, 11, 9"}, matcher.process(input));
    }

    @Test
    void testTwoFuzzy() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(twoFuzzy));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 11, 9", "4, 5, 6, 7, 8"}, matcher.process(input));
    }

    @Test
    void testThreeFuzzy() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(threeFuzzy));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 9", "4, 5, 7, 8"}, matcher.process(input));
    }

    @Test
    void testAllFuzzy() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(allFuzzy));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 9", "4, 7, 8"}, matcher.process(input));
        assertArrayEquals(new String[]{"1, 2", "10, 9", "4, 7, 8"}, matcher.process(input));
    }

    @Test
    void testDateParams() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(dateParams));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "4, 7, 8"}, matcher.process(input));
    }

    @Test
    void testValParams() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(valParams));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 9", "4, 7"}, matcher.process(input));
    }

    @Test
    void testVendorParams() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(vendorParams));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 11, 9", "4, 6, 7, 8"}, matcher.process(input));
    }

    @Test
    void testRefParams() {
        String input = Utils.concatDataPattern(datasetStr, Utils.patternListToJsonStr(refParams));
        GenericMatcher matcher = new GenericMatcher();
        assertArrayEquals(new String[]{"1, 2", "10, 9", "7, 8"}, matcher.process(input));
    }

}
