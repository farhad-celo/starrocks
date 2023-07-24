package com.celonis.invoicechecker;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class UDFJaroDistanceTest {
    final static double EPS = 1e-3;

    @Test
    void testFullMatch() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue(Math.abs(distance.evaluate("foobar", "foobar") - 0.0) < EPS);
    }

    @Test
    void testNotMatch() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue((1 - distance.evaluate("def", "abc")) < EPS);
    }

    @Test
    void testPartialMatach() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue(Math.abs(distance.evaluate("MARTHA", "MARHTA") - 0.055556) < EPS);
        assertTrue(Math.abs(distance.evaluate("DIXON", "DICKSONX") - 0.233334) < EPS);
        assertTrue(Math.abs(distance.evaluate("JELLYFISH", "SMELLYFISH") - 0.103704) < EPS);
        assertTrue(Math.abs(distance.evaluate("walmarttechnoligies", "walmart") - 0.210526) < EPS);
    }
}
