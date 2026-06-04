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

/**
 * A {@link RemoteFileInfoSource} for vended-credential Delta tables whose scan-file listing is consumed
 * lazily during scan-range scheduling, after the building method has returned.
 *
 * <p>It binds a throwaway-UGI {@link DeltaVendedFsScope} to the source's lifetime: the eager iterator
 * build and every later pull run under that scope, and {@link DeltaVendedFsScope.Handle#close} reclaims
 * the {@code S3AFileSystem} instances when the source is closed. Without it, each credential rotation
 * would leak one filesystem under the long-lived login UGI.</p>
 */
final class ScopedRemoteFileInfoSource implements RemoteFileInfoSource {
    private final DeltaVendedFsScope.Handle handle;
    private final CloseableIterator<Pair<FileScanTask, DeltaLakeAddFileStatsSerDe>> iterator;

    private ScopedRemoteFileInfoSource(DeltaVendedFsScope.Handle handle,
            CloseableIterator<Pair<FileScanTask, DeltaLakeAddFileStatsSerDe>> iterator) {
        this.handle = handle;
        this.iterator = iterator;
    }

    static RemoteFileInfoSource open(String scopeName,
            DeltaVendedFsScope.ScopedAction<CloseableIterator<Pair<FileScanTask, DeltaLakeAddFileStatsSerDe>>>
                    iteratorBuilder) {
        DeltaVendedFsScope.Handle handle = DeltaVendedFsScope.open(scopeName);
        CloseableIterator<Pair<FileScanTask, DeltaLakeAddFileStatsSerDe>> iterator;
        try {
            iterator = handle.callAs(iteratorBuilder);
        } catch (Exception e) {
            handle.close();
            throw asScanFilesException(e);
        }
        return new ScopedRemoteFileInfoSource(handle, iterator);
    }

    @Override
    public RemoteFileInfo getOutput() {
        try {
            return handle.callAs(() -> new DeltaRemoteFileInfo(iterator.next().first));
        } catch (Exception e) {
            throw asScanFilesException(e);
        }
    }

    @Override
    public boolean hasMoreOutput() {
        try {
            return handle.callAs(iterator::hasNext);
        } catch (Exception e) {
            throw asScanFilesException(e);
        }
    }

    @Override
    public void close() {
        try {
            iterator.close();
        } catch (Exception ignored) {
            // best-effort; the scope close below is what reclaims the filesystems
        } finally {
            handle.close();
        }
    }

    // doAs surfaces the original cause; preserve runtime exceptions (e.g. the StarRocksConnectorException
    // the iterator already throws on IO errors) and wrap only genuinely checked failures.
    private static RuntimeException asScanFilesException(Exception e) {
        if (e instanceof RuntimeException) {
            return (RuntimeException) e;
        }
        return new StarRocksConnectorException("Failed to get delta lake scan files", e);
    }
}
