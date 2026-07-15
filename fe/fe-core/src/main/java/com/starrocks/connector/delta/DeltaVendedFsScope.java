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
import java.util.regex.Pattern;

/**
 * Runs a vended-credentials Delta Kernel read under a throwaway {@link UserGroupInformation} so the
 * filesystem Hadoop builds is keyed on that UGI and reclaimed by {@link FileSystem#closeAllForUGI} when
 * the scope closes. Hadoop keys its cache on {@code (scheme, authority, UGI)} and ignores the lease, so
 * under the shared login UGI per-credential filesystems both leak and get reused across tables with the
 * wrong lease. Requires the filesystem cache on ({@code closeAllForUGI} only reclaims cached instances);
 * callers force it via {@link #enableFilesystemCache} on the throwaway per-table config.
 */
public final class DeltaVendedFsScope {
    private static final Logger LOG = LogManager.getLogger(DeltaVendedFsScope.class);

    // Cloud-filesystem schemes that may carry vended credentials; the cache is force-enabled for all so
    // closeAllForUGI can reclaim what a load builds. gs:// is listed for parity though Unity never vends GCS.
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

    // Per-thread count of scopes currently executing here. When > 0 the current UGI is already a
    // throwaway scope UGI, so a nested vended read can reuse it instead of opening a redundant scope.
    private static final ThreadLocal<Integer> ACTIVE_DEPTH = ThreadLocal.withInitial(() -> 0);

    private static final Pattern SCOPE_NAME = Pattern.compile("[A-Za-z0-9_-]+");

    @FunctionalInterface
    public interface ScopedAction<T> {
        T run() throws Exception;
    }

    private DeltaVendedFsScope() {
    }

    /** Whether a scope is running an action on this thread; drives {@link #runScopedReentrant}. */
    static boolean isActiveOnThread() {
        return ACTIVE_DEPTH.get() > 0;
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
        if (scopeName == null || !SCOPE_NAME.matcher(scopeName).matches()) {
            throw new IllegalArgumentException(
                    "scope name must be non-empty and match [A-Za-z0-9_-]: " + scopeName);
        }
        return new Handle(scopeName);
    }

    public static <T> T runScoped(String scopeName, ScopedAction<T> action) throws Exception {
        Handle handle = open(scopeName);
        try {
            return handle.callAs(action);
        } finally {
            handle.close();
        }
    }

    /**
     * Like {@link #runScoped} but reuses an already-active scope on this thread instead of nesting.
     * Only for reads always reached from within an enclosing scope covering the same credentials; a
     * read for an unrelated account/lease would inherit the enclosing filesystem, so use {@link #runScoped}.
     */
    public static <T> T runScopedReentrant(String scopeName, ScopedAction<T> action) throws Exception {
        if (isActiveOnThread()) {
            return action.run();
        }
        return runScoped(scopeName, action);
    }

    static String scopeNameFor(String catalog, String db, String table) {
        return ("delta-vended-fs-" + catalog + "-" + db + "-" + table).replaceAll("[^A-Za-z0-9_-]", "_");
    }

    // Backstop scope name for the shared meta cache when a loader runs unscoped. Pinned to the table
    // directory (above /_delta_log) for legibility; correctness only needs a fresh UGI per unscoped load.
    public static String scopeNameForPath(String path) {
        if (path == null || path.isEmpty()) {
            throw new IllegalArgumentException("path must be non-empty");
        }
        String base = path;
        int idx = base.indexOf("/_delta_log");
        if (idx > 0) {
            base = base.substring(0, idx);
        }
        return ("delta-vended-fs-" + base).replaceAll("[^A-Za-z0-9_-]", "_");
    }

    /** A live scope; {@link #callAs} may run on whichever thread pulls the iterator, since doAs is per-thread. */
    static final class Handle implements AutoCloseable {
        private final String scopeName;
        private final UserGroupInformation scopeUgi;

        private Handle(String scopeName) {
            this.scopeName = scopeName;
            // A fresh remote user yields a distinct Subject, hence a distinct FileSystem cache key, even
            // when two scopes share a name -- so concurrent loads never share (or close) each other's FS.
            this.scopeUgi = UserGroupInformation.createRemoteUser(scopeName);
        }

        <T> T callAs(ScopedAction<T> action) throws Exception {
            // Mark the scope active so nested vended reads on this thread reuse this UGI (see isActiveOnThread).
            ACTIVE_DEPTH.set(ACTIVE_DEPTH.get() + 1);
            try {
                // doAs unwraps PrivilegedActionException, rethrowing the original cause, so no catch is needed.
                return scopeUgi.doAs((PrivilegedExceptionAction<T>) action::run);
            } finally {
                int depth = ACTIVE_DEPTH.get() - 1;
                if (depth == 0) {
                    ACTIVE_DEPTH.remove();
                } else {
                    ACTIVE_DEPTH.set(depth);
                }
            }
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
