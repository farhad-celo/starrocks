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

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.security.UserGroupInformation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.io.IOException;
import java.security.PrivilegedExceptionAction;

/**
 * Runs a vended-credentials Delta Kernel read under a throwaway {@link UserGroupInformation} so that
 * Hadoop's {@link FileSystem} cache keys the {@code S3AFileSystem} it builds on that UGI.
 *
 * <p>Hadoop caches filesystems on {@code (scheme, authority, UGI)}, and StarRocks folds the rotating
 * vended credential into the key as well. Under the long-lived login UGI those per-credential entries
 * pile up in the static cache and are never reclaimed, so every credential rotation leaks one
 * {@code S3AFileSystem} (and its executor, HTTP, and credential-refresh threads). Binding each read to
 * a per-load UGI fixes this: the load is isolated from other tables sharing the bucket, and
 * {@link FileSystem#closeAllForUGI} reclaims every filesystem the load created the moment it returns.</p>
 *
 * <p>This relies on the Hadoop filesystem cache being enabled: {@code closeAllForUGI} can only reclaim
 * cached instances. The vended-credentials config is a throwaway per-table copy, so callers force the
 * cache on once at config creation via {@link #enableFilesystemCache}; nothing needs restoring.</p>
 */
final class DeltaVendedFsScope {
    private static final Logger LOG = LogManager.getLogger(DeltaVendedFsScope.class);

    // Every cloud-filesystem scheme that may carry vended credentials. The cache is force-enabled for
    // all of them so closeAllForUGI can reclaim whatever the load builds. gs:// is listed for parity;
    // Unity does not vend GCS credentials today, so it never enters this scope yet.
    private static final String[] CLOUD_FS_SCHEME_KEYS = {
            "fs.s3a.impl.disable.cache",
            "fs.s3.impl.disable.cache",
            "fs.s3n.impl.disable.cache",
            "fs.abfs.impl.disable.cache",
            "fs.abfss.impl.disable.cache",
            "fs.wasb.impl.disable.cache",
            "fs.wasbs.impl.disable.cache",
            "fs.gs.impl.disable.cache",
    };

    @FunctionalInterface
    interface ScopedAction<T> {
        T run() throws Exception;
    }

    private DeltaVendedFsScope() {
    }

    /**
     * Force the cloud filesystem caches on for {@code conf}.
     */
    static void enableFilesystemCache(Configuration conf) {
        for (String key : CLOUD_FS_SCHEME_KEYS) {
            conf.set(key, "false");
        }
    }

    /**
     * Open a scope whose lifetime the caller controls. Creates the throwaway UGI immediately; the
     * caller must run every filesystem-touching action via {@link Handle#callAs} and call
     * {@link Handle#close} exactly once when done, which reclaims the filesystems.
     */
    static Handle open(String scopeName) {
        return new Handle(scopeName);
    }

    static <T> T runScoped(String scopeName, ScopedAction<T> action) throws Exception {
        Handle handle = open(scopeName);
        try {
            return handle.callAs(action);
        } finally {
            handle.close();
        }
    }

    // The scope name becomes the synthetic UGI user name, which the patched Hadoop FileSystem parses
    // as a URI during S3AFileSystem init. Keep it to [A-Za-z0-9_-] so it cannot look like a
    // scheme-qualified URI (a ':' would trigger "Relative path in absolute URI"). Both the snapshot-load
    // and scan-file-listing sites build the name through here so the sanitization can never drift.
    static String scopeNameFor(String catalog, String db, String table) {
        return ("delta-vended-fs-" + catalog + "-" + db + "-" + table).replaceAll("[^A-Za-z0-9_-]", "_");
    }

    /**
     * A live scope. {@link #callAs} may be invoked from whichever thread happens to pull the iterator,
     * since {@code doAs} sets the UGI per-thread.
     */
    static final class Handle implements AutoCloseable {
        private final String scopeName;
        private final UserGroupInformation scopeUgi;

        private Handle(String scopeName) {
            this.scopeName = scopeName;
            // A fresh remote user yields a distinct Subject, hence a distinct FileSystem cache key, so
            // concurrent loads (and closeAllForUGI below) never collide even for the same table.
            this.scopeUgi = UserGroupInformation.createRemoteUser(scopeName);
        }

        <T> T callAs(ScopedAction<T> action) throws Exception {
            // Hadoop's UserGroupInformation.doAs unwraps PrivilegedActionException itself, rethrowing
            // the original cause (IOException/RuntimeException/etc.), so no catch is needed here.
            return scopeUgi.doAs((PrivilegedExceptionAction<T>) action::run);
        }

        @Override
        public void close() {
            try {
                FileSystem.closeAllForUGI(scopeUgi);
            } catch (IOException e) {
                LOG.warn("Failed to close filesystems for delta vended scope {}", scopeName, e);
            }
        }
    }
}
