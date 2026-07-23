// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.sql.optimizer.statistics;

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableMap;
import com.starrocks.common.Config;
import com.starrocks.common.Pair;

import java.nio.ByteBuffer;
import java.util.Comparator;

public final class ColumnDict {
    /**
     * Unsigned-byte lexicographic comparator. BE sorts dictionary strings via memcmp, which the C
     * standard defines to compare bytes as unsigned char. ByteBuffer.compareTo on JDK 8 instead
     * compares bytes as signed (Java 9 fixed this to unsigned), so any UTF-8 string with a high-bit
     * byte (Cyrillic, CJK, etc.) sorts the opposite way on the two sides. Always use this comparator
     * when ordering dictionary keys on the FE so the result matches BE regardless of JDK version.
     */
    public static final Comparator<ByteBuffer> UNSIGNED_LEX = (a, b) -> {
        int aPos = a.position();
        int bPos = b.position();
        int aLen = a.limit() - aPos;
        int bLen = b.limit() - bPos;
        int n = Math.min(aLen, bLen);
        for (int i = 0; i < n; i++) {
            int diff = (a.get(aPos + i) & 0xff) - (b.get(bPos + i) & 0xff);
            if (diff != 0) {
                return diff;
            }
        }
        return aLen - bLen;
    };

    private final ImmutableMap<ByteBuffer, Integer> dict;
    // olap table use time info as version info.
    // table on lake use num as version, collectedVersion means historical version num,
    // while version means version in current period.
    private final long collectedVersion;
    private long version;
    // Serialized dict bytes, precomputed so the cache weigher is O(1).
    private final int byteSize;

    public ColumnDict(ImmutableMap<ByteBuffer, Integer> dict, long version) {
        // TODO: The default value of low_cardinality_threshold is 255. Should we set the check size to 255 or 256?
        Preconditions.checkState(!dict.isEmpty() && dict.size() <= Config.low_cardinality_threshold + 1,
                "dict size %s is illegal", dict.size());
        this.dict = dict;
        this.collectedVersion = version;
        this.version = version;
        this.byteSize = computeByteSize(dict);
    }

    public ColumnDict(ImmutableMap<ByteBuffer, Integer> dict, long collectedVersion, long version) {
        this.dict = dict;
        this.collectedVersion = collectedVersion;
        this.version = version;
        this.byteSize = computeByteSize(dict);
    }

    private static int computeByteSize(ImmutableMap<ByteBuffer, Integer> dict) {
        int size = 0;
        for (ByteBuffer buf : dict.keySet()) {
            size += buf.limit() - buf.position() + Integer.BYTES; // string bytes + offset id
        }
        return size;
    }

    public int getByteSize() {
        return byteSize;
    }

    public ImmutableMap<ByteBuffer, Integer> getDict() {
        return dict;
    }

    public long getVersion() {
        return version;
    }

    public long getCollectedVersion() {
        return collectedVersion;
    }

    public int getDictSize() {
        return dict.size();
    }

    void updateVersion(long version) {
        this.version = version;
    }

    /**
     * Merge two dictionaries and return two new dictionaries.
     * The id of each dictionary is a continuous integer from 1 to n, where n is the number of words in the dictionary.
     * Ensure that the lexicographical order of the dictionary words is consistent with the order of the ids.
     * If the merged dictionary size exceeds Config.low_cardinality_threshold, return null.
     */
    public static Pair<ColumnDict, ColumnDict> merge(ColumnDict d1, ColumnDict d2) {
        final int n1 = d1.getDict().size() + 1;
        final int n2 = d2.getDict().size() + 1;
        ByteBuffer[] sortedKeys1 = new ByteBuffer[n1];
        ByteBuffer[] sortedKeys2 = new ByteBuffer[n2];
        d1.getDict().forEach((key, idx) -> sortedKeys1[idx] = key);
        d2.getDict().forEach((key, idx) -> sortedKeys2[idx] = key);

        ImmutableMap.Builder<ByteBuffer, Integer> builder =
                ImmutableMap.builderWithExpectedSize(Math.min(n1 + n2 - 2, Config.low_cardinality_threshold));
        int idx1 = 1;
        int idx2 = 1;
        int newIdx = 1;
        while (idx1 < n1 && idx2 < n2) {
            ByteBuffer key1 = sortedKeys1[idx1];
            ByteBuffer key2 = sortedKeys2[idx2];
            // Must use UNSIGNED_LEX here, not ByteBuffer.compareTo: BE sorted these dicts by
            // unsigned memcmp, and on JDK 8 ByteBuffer.compareTo is signed, which would walk the
            // arrays in the wrong order and emit duplicate keys.
            int cmp = UNSIGNED_LEX.compare(key1, key2);
            if (cmp == 0) {
                builder.put(key1, newIdx);
                idx1++;
                idx2++;
            } else if (cmp < 0) {
                builder.put(key1, newIdx);
                idx1++;
            } else {
                builder.put(key2, newIdx);
                idx2++;
            }
            newIdx++;
        }

        while (idx1 < n1) {
            builder.put(sortedKeys1[idx1++], newIdx++);
        }
        while (idx2 < n2) {
            builder.put(sortedKeys2[idx2++], newIdx++);
        }

        if (newIdx > Config.low_cardinality_threshold) {
            return null;
        }

        final ImmutableMap<ByteBuffer, Integer> newDict = builder.build();
        ColumnDict newD1 = new ColumnDict(newDict, d1.getCollectedVersion(), d1.getVersion());
        ColumnDict newD2 = new ColumnDict(newDict, d2.getCollectedVersion(), d2.getVersion());
        return new Pair<>(newD1, newD2);
    }
}
