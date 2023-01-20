package com.celonis.invoicechecker;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class UDFJaroDistanceTest {
    final static double EPS = 1e-3;

    @Test
    void testFullMatch() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue(Math.abs(distance.evaluate("foobar", "foobar") - 1) < EPS);
    }

    @Test
    void testNotMatch() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue(distance.evaluate("def", "abc") < EPS);
    }

    @Test
    void testPartialMatach() {
        UDFJaroDistance distance = new UDFJaroDistance();
        assertTrue(Math.abs(distance.evaluate("MARTHA", "MARHTA") - 0.944444)< EPS);
        assertTrue(Math.abs(distance.evaluate("DIXON", "DICKSONX") - 0.766666)< EPS);
        assertTrue(Math.abs(distance.evaluate("JELLYFISH", "SMELLYFISH") - 0.896296)< EPS);
        assertTrue(Math.abs(distance.evaluate("walmarttechnoligies", "walmart") - 0.7894736842105262)< EPS);
    }
}