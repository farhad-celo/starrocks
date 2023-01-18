package com.celonis.invoicechecker;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.Getter;
import lombok.RequiredArgsConstructor;

import java.util.List;
import java.util.Map;
import java.util.HashMap;
import java.util.ArrayList;

@NoArgsConstructor
public class TrieTree {
    @RequiredArgsConstructor
    private class Node {
        @Getter
        private final Character value;
        @Getter
        @Setter
        List<String> ids = new ArrayList<>();
        @Getter
        @Setter
        Map<Character, Node> children = new HashMap<>();
    }

    // Build up the trie tree. Note that we don't index the empty strings.
    public void add(String str, String id) {
        if (str.isEmpty()) return;
        Node currentNode = root;
        for (int i = 0; i < str.length(); i++) {
            if (currentNode.getChildren().containsKey(str.charAt(i))) {
                currentNode = currentNode.getChildren().get(str.charAt(i));
            } else {
                Node newNode = new Node(str.charAt(i));
                currentNode.getChildren().put(str.charAt(i), newNode);
                currentNode = newNode;
            }
        }
        currentNode.getIds().add(id);
    }

    // Finds all the prefix strings of `str`.
    public List<String> findPrefix(String str) {
        return findPrefixImpl(str, root);
    }

    public List<String> findPrefixWithFuzzyMatch(String str, int numFuzzyMatch) {
        return findPrefixWithFuzzyMatchImpl(str, numFuzzyMatch, root);
    }

    private List<String> findPrefixWithFuzzyMatchImpl(String str, int numFuzzyMatch, Node root) {
        if (str.isEmpty()) {
            return root.getIds();
        }
        if (numFuzzyMatch == 0) {
            return findPrefixImpl(str, root);
        }
        List<String> ids = new ArrayList<>(root.getIds());
        Map<Character, Integer> characterToSkipNum = new HashMap<>();
        for (int i = 0; i < str.length() && i <= numFuzzyMatch; i++) {
            if (characterToSkipNum.containsKey(str.charAt(i))) {
                continue;
            } else {
                characterToSkipNum.put(str.charAt(i), i);
            }
        }
        for (Character character : characterToSkipNum.keySet()) {
            if (!root.getChildren().containsKey(character)) continue;
            int numSkip = characterToSkipNum.get(character);
            ids.addAll(findPrefixWithFuzzyMatchImpl(str.substring(numSkip+1), numFuzzyMatch-numSkip,
                    root.getChildren().get(character)));
        }

        return ids;
    }

    private List<String> findPrefixImpl(String str, Node root) {
        List<String> ids = new ArrayList<>(root.getIds());
        Node currentNode = root;
        for (int i = 0; i < str.length(); i++) {
            if (currentNode.getChildren().containsKey(str.charAt(i))) {
                currentNode = currentNode.getChildren().get(str.charAt(i));
            } else {
                // No way to advance.
                return ids;
            }
            ids.addAll(currentNode.getIds());
        }

        return ids;
    }

    private Node root = new Node(null);
}
