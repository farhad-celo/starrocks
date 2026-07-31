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

package com.starrocks.sql.analyzer.celonis;

import com.starrocks.qe.ConnectContext;
import com.starrocks.server.celonis.CelostarExtensionSet;
import com.starrocks.server.celonis.explaininputcolumns.CelostarSchemaExtension;

/**
 * Statement-scoped installation of Celostar schema extensions on the {@link ConnectContext}. The extensions are only
 * visible while the scope is open so normal statements keep the unextended catalog. Restores the previous extension set
 * on {@link #close()}, which makes this usable both in try-with-resources (authorization) and try/finally (analysis).
 */
public final class CelostarExtensionScope implements AutoCloseable {
    private final ConnectContext context;
    private final CelostarExtensionSet previousCelostarExtensions;

    private CelostarExtensionScope(ConnectContext context, CelostarExtensionSet previousCelostarExtensions) {
        this.context = context;
        this.previousCelostarExtensions = previousCelostarExtensions;
    }

    public static CelostarExtensionScope install(ConnectContext context, CelostarSchemaExtension schemaExtension) {
        CelostarExtensionSet previousCelostarExtensions = context.getCelostarExtensions();
        context.setCelostarExtensions(CelostarExtensionSet.ofSchemaExtension(schemaExtension));
        return new CelostarExtensionScope(context, previousCelostarExtensions);
    }

    /**
     * Temporarily removes any installed extensions, e.g. to re-resolve references against the unextended catalog.
     */
    public static CelostarExtensionScope clear(ConnectContext context) {
        CelostarExtensionSet previousCelostarExtensions = context.getCelostarExtensions();
        context.setCelostarExtensions(null);
        return new CelostarExtensionScope(context, previousCelostarExtensions);
    }

    @Override
    public void close() {
        context.setCelostarExtensions(previousCelostarExtensions);
    }
}
