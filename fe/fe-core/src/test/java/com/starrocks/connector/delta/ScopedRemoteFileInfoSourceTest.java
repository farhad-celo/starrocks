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

package com.starrocks.connector.delta;

import com.starrocks.common.Pair;
import com.starrocks.connector.RemoteFileInfo;
import com.starrocks.connector.RemoteFileInfoSource;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.delta.kernel.utils.CloseableIterator;
import org.apache.hadoop.security.UserGroupInformation;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class ScopedRemoteFileInfoSourceTest {

    @Test
    public void testIteratorIsBuiltUnderScopeUgi() throws Exception {
        String scope = "scope-build";
        List<String> buildUserSeen = new ArrayList<>();

        RecordingIterator iterator = new RecordingIterator(Collections.emptyList());
        RemoteFileInfoSource source = ScopedRemoteFileInfoSource.open(scope, () -> {
            buildUserSeen.add(UserGroupInformation.getCurrentUser().getUserName());
            return iterator;
        });

        Assertions.assertEquals(scope, buildUserSeen.get(0), "iterator build must run under the scope UGI");
        source.close();
    }

    @Test
    public void testPullsRunUnderScopeUgiAndWrapEachTask() throws Exception {
        String scope = "scope-pull";
        FileScanTask first = task();
        FileScanTask second = task();
        RecordingIterator iterator = new RecordingIterator(List.of(first, second));

        RemoteFileInfoSource source = ScopedRemoteFileInfoSource.open(scope, () -> iterator);

        Assertions.assertTrue(source.hasMoreOutput());
        RemoteFileInfo out1 = source.getOutput();
        RemoteFileInfo out2 = source.getOutput();
        Assertions.assertFalse(source.hasMoreOutput());

        Assertions.assertTrue(out1 instanceof DeltaRemoteFileInfo);
        Assertions.assertSame(first, ((DeltaRemoteFileInfo) out1).getFileScanTask());
        Assertions.assertSame(second, ((DeltaRemoteFileInfo) out2).getFileScanTask());

        for (String user : iterator.userSeen) {
            Assertions.assertEquals(scope, user, "every iterator pull must run under the scope UGI");
        }
        source.close();
    }

    @Test
    public void testCloseClosesIterator() throws Exception {
        RecordingIterator iterator = new RecordingIterator(Collections.emptyList());

        RemoteFileInfoSource source = ScopedRemoteFileInfoSource.open("scope-close", () -> iterator);
        source.close();

        Assertions.assertTrue(iterator.closed, "underlying iterator must be closed");
    }

    @Test
    public void testBuildFailureWrapsCheckedException() {
        IOException boom = new IOException("build boom");

        StarRocksConnectorException thrown = Assertions.assertThrows(StarRocksConnectorException.class, () ->
                ScopedRemoteFileInfoSource.open("scope-build-checked", () -> {
                    throw boom;
                }));

        Assertions.assertSame(boom, thrown.getCause(), "checked build failure must be wrapped, preserving the cause");
    }

    @Test
    public void testBuildFailurePropagatesRuntimeException() {
        IllegalStateException boom = new IllegalStateException("build boom");

        IllegalStateException thrown = Assertions.assertThrows(IllegalStateException.class, () ->
                ScopedRemoteFileInfoSource.open("scope-build-runtime", () -> {
                    throw boom;
                }));

        Assertions.assertSame(boom, thrown, "runtime build failure must propagate unwrapped");
    }

    @Test
    public void testPullPropagatesRuntimeExceptionUnwrapped() throws Exception {
        RecordingIterator iterator = new RecordingIterator(List.of(task()));
        iterator.nextError = new IllegalStateException("next boom");

        RemoteFileInfoSource source = ScopedRemoteFileInfoSource.open("scope-pull-error", () -> iterator);

        IllegalStateException thrown = Assertions.assertThrows(IllegalStateException.class, source::getOutput);
        Assertions.assertSame(iterator.nextError, thrown, "runtime pull failure must propagate unwrapped");
        source.close();
    }

    @Test
    public void testCloseIsBestEffortWhenIteratorCloseThrows() {
        RecordingIterator iterator = new RecordingIterator(Collections.emptyList());
        iterator.failOnClose = true;

        RemoteFileInfoSource source = ScopedRemoteFileInfoSource.open("scope-close-throws", () -> iterator);

        Assertions.assertDoesNotThrow(source::close, "a failing iterator close must not escape");
    }

    private static FileScanTask task() {
        return new FileScanTask(null, 0L, Collections.emptyMap(), null);
    }

    private static final class RecordingIterator
            implements CloseableIterator<Pair<FileScanTask, DeltaLakeAddFileStatsSerDe>> {
        private final java.util.Iterator<FileScanTask> tasks;
        final List<String> userSeen = new ArrayList<>();
        boolean closed = false;
        boolean failOnClose = false;
        RuntimeException nextError;

        RecordingIterator(List<FileScanTask> tasks) {
            this.tasks = tasks.iterator();
        }

        @Override
        public boolean hasNext() {
            recordUser();
            return tasks.hasNext();
        }

        @Override
        public Pair<FileScanTask, DeltaLakeAddFileStatsSerDe> next() {
            recordUser();
            if (nextError != null) {
                throw nextError;
            }
            return Pair.create(tasks.next(), null);
        }

        @Override
        public void close() {
            closed = true;
            if (failOnClose) {
                throw new RuntimeException("close boom");
            }
        }

        private void recordUser() {
            try {
                userSeen.add(UserGroupInformation.getCurrentUser().getUserName());
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }
    }
}
