package com.celonis.invoicechecker;

import org.junit.jupiter.api.Test;

import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class TrieTreeTest {
    @Test
    void testEmptyness() {
        TrieTree trie = new TrieTree();
        assertTrue(trie.findPrefix("abc").isEmpty());
        trie.add("", "1L");
        assertTrue(trie.findPrefix("abc").isEmpty());
        assertTrue(trie.findPrefix("").isEmpty());
    }

    @Test void testPrefix() {
        TrieTree trie = new TrieTree();
        assertTrue(trie.findPrefix("abc").isEmpty());
        trie.add("a", "1L");
        trie.add("b", "2L");
        trie.add("c", "3L");
        trie.add("ab", "4L");
        trie.add("abc", "5L");
        trie.add("abcd", "6L");
        assertTrue(listEqualsIgnoringOrder(Arrays.asList("1L", "4L", "5L"), trie.findPrefix("abc")));
    }

    @Test void testPrefixWithFuzzyMatch() {
        {
            // Fuzzy match 0.
            TrieTree trie = new TrieTree();
            assertTrue(trie.findPrefix("abc").isEmpty());
            trie.add("a", "1L");
            trie.add("b", "2L");
            trie.add("c", "3L");
            trie.add("ab", "4L");
            trie.add("abc", "5L");
            trie.add("abcd", "6L");
            assertTrue(listEqualsIgnoringOrder(Arrays.asList("1L", "4L", "5L"), trie.findPrefixWithFuzzyMatch("abc", 0)));
        }
        {
            // Fuzzy match 1.
            TrieTree trie = new TrieTree();
            assertTrue(trie.findPrefix("abc").isEmpty());
            trie.add("a", "1L");
            trie.add("b", "2L");
            trie.add("c", "3L");
            trie.add("ab", "4L");
            trie.add("abc", "5L");
            trie.add("abcd", "6L");
            assertTrue(listEqualsIgnoringOrder(Arrays.asList("1L", "2L", "4L", "5L"), trie.findPrefixWithFuzzyMatch("abc", 1)));
        }
        {
            // Fuzzy match 2.
            TrieTree trie = new TrieTree();
            assertTrue(trie.findPrefix("abcd").isEmpty());
            trie.add("a", "1L");
            trie.add("b", "2L");
            trie.add("c", "3L");
            trie.add("ab", "4L");
            trie.add("abc", "5L");
            trie.add("abcd", "6L");
            trie.add("ad", "7L");
            trie.add("ae", "8L");
            trie.add("abcde", "9L");
            trie.add("d", "10L");
            assertTrue(listEqualsIgnoringOrder(Arrays.asList("1L", "2L", "3L", "4L", "5L", "6L", "7L"), trie.findPrefixWithFuzzyMatch("abcd", 2)));
        }
    }

    boolean listEqualsIgnoringOrder(List<String> list1, List<String> list2) {
        return list1.size() == list2.size() && list1.containsAll(list2) && list2.containsAll(list1);
    }
}