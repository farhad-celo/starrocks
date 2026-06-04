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
import org.apache.hadoop.security.UserGroupInformation;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.io.IOException;

public class DeltaVendedFsScopeTest {

    @Test
    public void testEnableFilesystemCacheForcesEveryCloudSchemeOn() {
        Configuration conf = new Configuration(false);
        conf.set("fs.s3a.impl.disable.cache", "true");

        DeltaVendedFsScope.enableFilesystemCache(conf);

        for (String key : new String[] {
                "fs.s3a.impl.disable.cache", "fs.s3.impl.disable.cache", "fs.s3n.impl.disable.cache",
                "fs.abfs.impl.disable.cache", "fs.abfss.impl.disable.cache",
                "fs.wasb.impl.disable.cache", "fs.wasbs.impl.disable.cache", "fs.gs.impl.disable.cache"}) {
            Assertions.assertEquals("false", conf.get(key), key + " must be forced to false");
        }
    }

    @Test
    public void testActionRunsUnderThrowawayUgiNamedAfterScope() throws Exception {
        String scopeName = "delta-vended-fs-scope-ugi";

        String userInside = DeltaVendedFsScope.runScoped(scopeName, () ->
                UserGroupInformation.getCurrentUser().getUserName());

        Assertions.assertEquals(scopeName, userInside,
                "action must run under a remote UGI named after the scope");
    }

    @Test
    public void testReturnValuePropagates() throws Exception {
        Assertions.assertEquals("ok", DeltaVendedFsScope.runScoped("scope-return", () -> "ok"));
    }

    @Test
    public void testCheckedExceptionPropagatesUnwrapped() {
        IOException boom = new IOException("boom");

        IOException thrown = Assertions.assertThrows(IOException.class, () ->
                DeltaVendedFsScope.runScoped("scope-throw", () -> {
                    throw boom;
                }));

        Assertions.assertSame(boom, thrown, "checked exception must propagate unwrapped");
    }

    @Test
    public void testHandleCallAsRunsUnderScopeUgi() throws Exception {
        String scopeName = "scope-handle-ugi";
        DeltaVendedFsScope.Handle handle = DeltaVendedFsScope.open(scopeName);
        try {
            String userInside = handle.callAs(() -> UserGroupInformation.getCurrentUser().getUserName());
            Assertions.assertEquals(scopeName, userInside);
        } finally {
            handle.close();
        }
    }

    @Test
    public void testHandleCloseIsIdempotent() {
        DeltaVendedFsScope.Handle handle = DeltaVendedFsScope.open("scope-handle-idempotent");
        handle.close();
        Assertions.assertDoesNotThrow(handle::close);
    }
}
